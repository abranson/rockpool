/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.linux.InactiveLinuxDeviceActivity
import io.rebble.libpebblecommon.linux.LinuxDeviceActivity
import io.rebble.libpebblecommon.linux.notifications.LinuxNotification
import io.rebble.libpebblecommon.linux.notifications.LinuxNotificationBackend
import io.rebble.libpebblecommon.linux.notifications.LinuxNotificationCommand
import io.rebble.libpebblecommon.linux.notifications.LinuxNotificationEvent
import io.rebble.libpebblecommon.linux.notifications.MappedNotification
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.time.Instant

internal data class PlatformNotificationFilter(
    val enabled: Boolean,
    val name: String,
    val icon: String,
    val suppressWhenActive: Boolean = false,
) {
    init {
        require(enabled || !suppressWhenActive)
    }

    val mode: Int
        get() = when {
            !enabled -> NOTIFICATION_DISABLED
            suppressWhenActive -> NOTIFICATION_DISABLED_WHEN_ACTIVE
            else -> NOTIFICATION_ENABLED
        }

    companion object {
        fun fromMode(mode: Int, name: String, icon: String): PlatformNotificationFilter =
            when (mode) {
                NOTIFICATION_DISABLED -> PlatformNotificationFilter(false, name, icon)
                NOTIFICATION_DISABLED_WHEN_ACTIVE ->
                    PlatformNotificationFilter(true, name, icon, suppressWhenActive = true)
                NOTIFICATION_ENABLED -> PlatformNotificationFilter(true, name, icon)
                else -> throw IllegalArgumentException("invalid notification mode $mode")
            }
    }
}

internal const val NOTIFICATION_DISABLED = 0
internal const val NOTIFICATION_DISABLED_WHEN_ACTIVE = 1
internal const val NOTIFICATION_ENABLED = 2

internal const val GLOBAL_NOTIFICATION_FILTER_PREFIX = "notifications."
internal const val GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING =
    "${GLOBAL_NOTIFICATION_FILTER_PREFIX}configured"

internal fun hasCanonicalNotificationFilters(values: Map<String, String>): Boolean =
    values[GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING] == "true" ||
        values.keys.any { it.endsWith(".source") }

internal fun storedNotificationMode(value: String?): Int =
    value?.toIntOrNull()?.takeIf { it in NOTIFICATION_DISABLED..NOTIFICATION_ENABLED }
        ?: value?.toBooleanStrictOrNull()?.let {
            if (it) NOTIFICATION_ENABLED else NOTIFICATION_DISABLED
        }
        ?: NOTIFICATION_ENABLED

internal fun storedNotificationEnabled(value: String?): Boolean =
    storedNotificationMode(value) != NOTIFICATION_DISABLED

internal fun notificationFiltersFromEntries(
    values: Map<String, String>,
): Map<String, PlatformNotificationFilter> = buildMap {
    values.entries
        .asSequence()
        .filter { it.key.endsWith(".source") && it.value.isNotBlank() }
        .sortedBy(Map.Entry<String, String>::key)
        .forEach { (sourceKey, source) ->
            val prefix = sourceKey.removeSuffix(".source")
            putIfAbsent(
                source,
                PlatformNotificationFilter.fromMode(
                    mode = storedNotificationMode(values["$prefix.enabled"]),
                    name = values["$prefix.name"].orEmpty().ifEmpty { source },
                    icon = values["$prefix.icon"].orEmpty(),
                ),
            )
        }
}

internal fun loadPlatformNotificationFilters(
    settings: RockpoolSettings,
): Map<String, PlatformNotificationFilter> {
    val canonical = settings.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
    if (hasCanonicalNotificationFilters(canonical)) {
        return notificationFiltersFromEntries(canonical)
    }
    // One-release migration fallback. Existing draft state was stored below a
    // watch object even though libpebble3 notification delivery is global.
    return notificationFiltersFromEntries(
        settings.entries("watch.").filterKeys { ".notifications." in it }
    )
}

internal fun mapPlatformNotificationEvent(
    event: PlatformNotificationEvent,
    filters: Map<String, PlatformNotificationFilter>,
    deviceActive: Boolean = false,
): LinuxNotificationEvent? = when (event) {
    is PlatformNotificationEvent.Posted -> {
        val filter = filters[event.applicationId]
        if (filter?.enabled == false || filter?.suppressWhenActive == true && deviceActive) {
            event.replacesId.takeIf(String::isNotEmpty)?.let {
                LinuxNotificationEvent.Closed(it)
            }
        } else {
            LinuxNotificationEvent.Posted(
                LinuxNotification(
                    id = event.id,
                    replacesId = event.replacesId.ifEmpty { null },
                    timestamp = event.timestampMs.takeIf { it > 0 }
                        ?.let(Instant::fromEpochMilliseconds),
                    content = MappedNotification(
                        title = event.title,
                        body = event.body,
                        packageName = event.applicationId,
                        appName = filter?.name?.ifEmpty { null }
                            ?: event.applicationName.ifEmpty { event.applicationId },
                        androidPackageName = event.applicationId,
                        category = event.category
                            .removePrefix(SAILFISH_CATEGORY_PREFIX)
                            .ifEmpty { null },
                        iconName = filter?.icon?.ifEmpty { null }
                            ?: event.iconName.ifEmpty { null },
                    ),
                    hasDefaultAction = event.flags and
                        PlatformProviderController.NOTIFICATION_HAS_DEFAULT_ACTION != 0,
                )
            )
        }
    }

    is PlatformNotificationEvent.Closed ->
        LinuxNotificationEvent.Closed(event.id, event.reason.toUInt())
}

/** Provider-backed transport for libpebble3's native-Linux notification pipeline. */
internal class PlatformNotificationBackend(
    private val controller: PlatformProviderController,
    initialFilters: Map<String, PlatformNotificationFilter> = emptyMap(),
    private val deviceActivity: LinuxDeviceActivity = InactiveLinuxDeviceActivity,
    eventCapacity: Int = EVENT_CAPACITY,
    private val filterApplyTimeoutMs: Long = FILTER_APPLY_TIMEOUT_MS,
    private val executeCommand: suspend (Int, String, () -> Boolean) -> Int? =
        controller::notificationCommand,
) : LinuxNotificationBackend {
    private val logger = Logger.withTag("PlatformNotificationBackend")
    private val eventChannel = Channel<LinuxNotificationEvent>(eventCapacity)
    private val eventLock = Any()
    private val notificationsReady = AtomicBoolean(false)
    private val filters = AtomicReference(initialFilters.toMap())
    private var actionEpoch = 0L
    private var resetQueued = false
    private val resetWaiters = mutableListOf<CompletableDeferred<Unit>>()

    init {
        require(eventCapacity > 0)
        require(filterApplyTimeoutMs > 0)
        controller.addListener(::providerSnapshotChanged)
        controller.addNotificationListener(::providerNotification)
    }

    override fun events(): Flow<LinuxNotificationEvent> = flow {
        for (event in eventChannel) {
            if (event === LinuxNotificationEvent.Reset) {
                try {
                    emit(event)
                } finally {
                    // A cancelled collector has already removed Reset from the
                    // channel.  Always release the barrier so a replacement
                    // collector can receive later events instead of leaving
                    // the backend permanently wedged in resetQueued.
                    resetDelivered()
                }
            } else {
                emit(event)
            }
        }
    }

    internal fun providerSnapshotChanged(snapshot: PlatformProviderSnapshot) {
        val ready = snapshot.domains and NOTIFICATION_DOMAIN != 0L
        synchronized(eventLock) {
            if (notificationsReady.getAndSet(ready) && !ready) {
                queueResetLocked()
            }
        }
    }

    internal fun providerNotification(event: PlatformNotificationEvent) {
        synchronized(eventLock) {
            if (!notificationsReady.get() || resetQueued) return
            val deviceActive = runCatching(deviceActivity::isActiveAndUnlocked).getOrDefault(false)
            val mapped = mapPlatformNotificationEvent(event, filters.get(), deviceActive) ?: return
            if (eventChannel.trySend(mapped).isFailure) {
                logger.e { "native notification event queue is full; resetting state" }
                queueResetLocked()
            }
        }
    }

    /**
     * Installs the policy synchronously and waits a bounded time for existing
     * pins to be retired.  A false result means only that cleanup is pending;
     * new provider events already use [newFilters].
     */
    suspend fun replaceFilters(newFilters: Map<String, PlatformNotificationFilter>): Boolean {
        val snapshot = newFilters.toMap()
        val applied = synchronized(eventLock) {
            val previous = filters.getAndSet(snapshot)
            val changed = (previous.keys + snapshot.keys).filterTo(linkedSetOf()) {
                (previous[it]?.mode ?: NOTIFICATION_ENABLED) !=
                    (snapshot[it]?.mode ?: NOTIFICATION_ENABLED)
            }
            if (changed.isEmpty()) {
                null
            } else {
                CompletableDeferred<Unit>().also { deferred ->
                    if (resetQueued) {
                        resetWaiters += deferred
                    } else {
                        val event = LinuxNotificationEvent.SourcesChanged(changed, deferred)
                        if (eventChannel.trySend(event).isFailure) {
                            queueResetLocked(deferred)
                        }
                    }
                }
            }
        }
        return applied == null || withTimeoutOrNull(filterApplyTimeoutMs) {
            applied.await()
            true
        } == true
    }

    override suspend fun execute(
        id: String,
        command: LinuxNotificationCommand,
    ): Boolean {
        val nativeCommand = when (command) {
            LinuxNotificationCommand.Dismiss ->
                PlatformProviderController.NOTIFICATION_DISMISS
            LinuxNotificationCommand.Open -> PlatformProviderController.NOTIFICATION_OPEN
        }
        val epoch = synchronized(eventLock) {
            if (!notificationsReady.get() || resetQueued) return false
            actionEpoch
        }
        return executeCommand(nativeCommand, id) {
            synchronized(eventLock) {
                notificationsReady.get() && !resetQueued && actionEpoch == epoch
            }
        } == STATUS_OK
    }

    /**
     * Replace a lossy queue state with one reliable barrier. Events arriving
     * before the listener consumes Reset are deliberately dropped: without a
     * provider snapshot they cannot safely retain native action authority.
     */
    private fun queueResetLocked(waiter: CompletableDeferred<Unit>? = null) {
        waiter?.let(resetWaiters::add)
        actionEpoch++
        if (resetQueued) return
        while (true) {
            when (val event = eventChannel.tryReceive().getOrNull()) {
                is LinuxNotificationEvent.SourcesChanged -> resetWaiters += event.applied
                null -> break
                else -> Unit
            }
        }
        resetQueued = true
        check(eventChannel.trySend(LinuxNotificationEvent.Reset).isSuccess) {
            "open notification event queue rejected reset after drain"
        }
    }

    private fun resetDelivered() {
        val waiters = synchronized(eventLock) {
            resetQueued = false
            resetWaiters.toList().also { resetWaiters.clear() }
        }
        waiters.forEach { it.complete(Unit) }
    }

    private companion object {
        private const val EVENT_CAPACITY = 64
        private const val FILTER_APPLY_TIMEOUT_MS = 15_000L
        private const val STATUS_OK = 0
        private const val NOTIFICATION_DOMAIN = 1L
    }
}

private const val SAILFISH_CATEGORY_PREFIX = "x-nemo."
