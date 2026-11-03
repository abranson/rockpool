/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.get
import io.ktor.client.request.parameter
import io.ktor.client.statement.bodyAsText
import io.ktor.http.isSuccess
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.metadata.WatchType
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.rockpool.rockpoolApplicationIsInstalledOnWatch
import kotlinx.coroutines.CancellationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
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
)

internal fun rockpoolAppUpdateTargets(
    apps: List<LockerWrapper>,
    watches: List<Pair<String, WatchType>>,
): List<RockpoolAppUpdateTarget> = apps.filterIsInstance<LockerWrapper.NormalApp>().flatMap { app ->
    val properties = app.properties
    val storeId = properties.storeId?.takeIf { Regex("[0-9a-fA-F]{24}").matches(it) }
    val version = properties.version?.takeIf { it.isNotBlank() }
    if (storeId == null || version == null) emptyList() else watches.mapNotNull { (address, type) ->
        if (!rockpoolApplicationIsInstalledOnWatch(app, type)) null else RockpoolAppUpdateTarget(
            properties.id, storeId, properties.title, version, properties.type, address, type.codename,
        )
    }
}

/** Match the installed-list version comparison, but reject unrelated or incompatible store data. */
internal fun rockpoolAppUpdateVersion(body: String, target: RockpoolAppUpdateTarget): String? {
    val records = Json.parseToJsonElement(body).jsonObject["data"]?.jsonArray ?: return null
    val record = records.map { it.jsonObject }.firstOrNull {
        it["uuid"]?.jsonPrimitive?.contentOrNull.equals(target.uuid.toString(), ignoreCase = true)
    } ?: return null
    val compatibility = record["compatibility"]?.jsonObject ?: return null
    val variants = WatchType.fromCodename(target.hardware)?.getCompatibleAppVariants() ?: return null
    if (variants.none {
        compatibility[it.codename]?.jsonObject?.get("supported")?.jsonPrimitive?.booleanOrNull == true
    }) return null
    return record["latest_release"]?.jsonObject?.get("version")?.jsonPrimitive?.contentOrNull
        ?.takeIf { it.isNotBlank() }
}

private val appUpdateClient by lazy {
    HttpClient(CIO) {
        install(HttpTimeout) {
            requestTimeoutMillis = 15_000L
            connectTimeoutMillis = 5_000L
            socketTimeoutMillis = 10_000L
        }
    }
}

private suspend fun lookupAppUpdate(target: RockpoolAppUpdateTarget): String? {
    val response = appUpdateClient.get("https://appstore-api.rebble.io/api/v1/apps/id/${target.storeId}") {
        parameter("hardware", target.hardware)
    }
    check(response.status.isSuccess()) { "Store lookup returned ${response.status}" }
    return rockpoolAppUpdateVersion(response.bodyAsText(), target)
}

/** Called serially by the locker observer. Successful lookups are reused for six hours. */
internal class RockpoolAppUpdateNotification(
    private val settings: RockpoolSettings,
    private val lookup: suspend (RockpoolAppUpdateTarget) -> String? = ::lookupAppUpdate,
    private val publish: suspend (RockpoolAppUpdateTarget, String, () -> Boolean) -> Boolean =
        ::publishAppUpdateNotification,
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
) {
    private data class CacheKey(val uuid: Uuid, val storeId: String, val hardware: String, val installedVersion: String)
    private data class CachedVersion(val version: String?, val expires: Long)
    private val cache = mutableMapOf<CacheKey, CachedVersion>()
    private val logger = Logger.withTag("RockpoolAppUpdates")

    suspend fun update(
        targets: List<RockpoolAppUpdateTarget>,
        stillInstalled: suspend (RockpoolAppUpdateTarget) -> Boolean,
    ) {
        cache.keys.retainAll(targets.map { CacheKey(it.uuid, it.storeId, it.hardware, it.version) }.toSet())
        for (target in targets) {
            val cacheKey = CacheKey(target.uuid, target.storeId, target.hardware, target.version)
            val checkedAt = now()
            try {
                val cached = cache[cacheKey]?.takeIf { checkedAt < it.expires } ?: run {
                    CachedVersion(lookup(target), checkedAt + 6 * 60 * 60 * 1000L).also { cache[cacheKey] = it }
                }
                val version = cached.version ?: continue
                if (version == target.version) continue
                // The locker is account-global: one alert per app release, even with several watches.
                val key = "app.${target.uuid}.notifiedVersion"
                if (settings.get(key) == version || !stillInstalled(target)) continue
                publish(target, version) { settings.setChecked(key, version) }
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
