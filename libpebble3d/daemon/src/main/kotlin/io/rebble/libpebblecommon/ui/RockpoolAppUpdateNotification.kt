/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.linux.web.AppstoreListingId
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.metadata.WatchType
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.rockpool.rockpoolApplicationIsInstalledOnWatch
import kotlinx.coroutines.CancellationException
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import kotlin.uuid.Uuid

internal data class RockpoolAppUpdateTarget(
    val uuid: Uuid,
    val storeId: String,
    val name: String,
    val version: String,
    val type: AppType,
    val address: String,
    val hardware: String,
    val pbwUrl: String? = null,
)

internal fun rockpoolAppUpdateTargets(
    apps: List<LockerWrapper>,
    watches: List<Pair<String, WatchType>>,
): List<RockpoolAppUpdateTarget> = apps.filterIsInstance<LockerWrapper.NormalApp>().flatMap { app ->
    val properties = app.properties
    val storeId = properties.storeId?.let(AppstoreListingId::parse)?.value
    val version = properties.version?.takeIf { it.isNotBlank() }
    if (storeId == null || version == null) emptyList() else watches.mapNotNull { (address, type) ->
        if (!rockpoolApplicationIsInstalledOnWatch(app, type)) null else RockpoolAppUpdateTarget(
            properties.id, storeId, properties.title, version, properties.type, address, type.codename,
            properties.pbwUrl,
        )
    }
}

/** Called serially by the locker observer. Successful lookups are reused for six hours. */
internal class RockpoolAppUpdateNotification(
    private val settings: RockpoolSettings,
    private val appStore: RockpoolAppStore,
    private val publish: suspend (RockpoolAppUpdateTarget, String, () -> Boolean) -> Boolean =
        ::publishAppUpdateNotification,
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
) {
    private data class CacheKey(val target: RockpoolAppUpdateTarget, val source: RockpoolAppStoreSource)
    private data class CachedVersion(val version: String?, val expires: Long)
    private val cache = mutableMapOf<CacheKey, CachedVersion>()
    private var previousSelection: RockpoolAppStore.Selection? = null
    private val logger = Logger.withTag("RockpoolAppUpdates")

    suspend fun update(
        targets: List<RockpoolAppUpdateTarget>,
        selection: RockpoolAppStore.Selection,
        stillInstalled: suspend (RockpoolAppUpdateTarget) -> Boolean,
    ) {
        if (previousSelection !== selection) {
            cache.clear()
            previousSelection = selection
        }
        cache.keys.retainAll(targets.map { CacheKey(it, selection.source) }.toSet())
        for (target in targets) {
            if (!appStore.isCurrent(selection)) return
            val cacheKey = CacheKey(target, selection.source)
            val checkedAt = now()
            try {
                val cached = cache[cacheKey]?.takeIf { checkedAt < it.expires } ?: run {
                    val version = appStore.lookup(selection, target)?.takeIf { it.updateAvailable }?.version
                    CachedVersion(version, checkedAt + 6 * 60 * 60 * 1000L).also { cache[cacheKey] = it }
                }
                val version = cached.version ?: continue
                // The locker is account-global: one alert per store and release, even with several watches.
                val key = "app.${target.uuid}.notifiedVersion.${selection.source.value}"
                val previousRebbleVersion = if (selection.source == RockpoolAppStoreSource.Rebble) {
                    settings.get("app.${target.uuid}.notifiedVersion")
                } else ""
                if (settings.get(key, previousRebbleVersion) == version || !stillInstalled(target) ||
                    !appStore.isCurrent(selection)
                ) continue
                publish(target, version) { appStore.rememberUpdate(selection, key, version) }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                // Retry failed lookups on the next periodic tick, without a loop on locker changes.
                cache[cacheKey] = CachedVersion(null, checkedAt + 15 * 60 * 1000L)
                logger.w { "app update notification failed for ${target.uuid}: ${e.message}" }
            }
        }
    }
}

internal fun appUpdateNotificationAction(target: RockpoolAppUpdateTarget): String =
    rockpoolNotificationAction(
        "OpenAppUpdate", target.address, target.uuid.toString(),
    )

private suspend fun publishAppUpdateNotification(
    target: RockpoolAppUpdateTarget, version: String, remember: () -> Boolean,
): Boolean {
    val connection = DBusConnectionBuilder.forSessionBus().withShared(false).build()
    try {
        val notifications = connection.getRemoteObject(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            RockpoolBatteryNotifications::class.java,
        )
        val summary = if (target.type == AppType.Watchface) "Watchface update available" else "Watchapp update available"
        val body = "${target.name} — ${target.version} → $version"
        val id = notifications.Notify(
            "Rockpool", UInt32(0), "rockpool", summary, body,
            listOf("default", "Open updates"),
            mapOf(
                "desktop-entry" to Variant("rockpool"),
                "category" to Variant("device"),
                "x-nemo-preview-summary" to Variant(summary),
                "x-nemo-preview-body" to Variant(body),
                "x-nemo-remote-action-default" to Variant(appUpdateNotificationAction(target)),
            ), -1,
        )
        if (id.toLong() == 0L) return false
        if (remember()) return true
        notifications.CloseNotification(id)
        return false
    } finally {
        connection.disconnect()
    }
}
