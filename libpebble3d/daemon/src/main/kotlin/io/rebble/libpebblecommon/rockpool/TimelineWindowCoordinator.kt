/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.endpointmanager.blobdb.TimelineWindow
import io.rebble.libpebblecommon.connection.endpointmanager.blobdb.TimelineWindowProvider
import io.rebble.libpebblecommon.connection.endpointmanager.blobdb.normalizedTimelineWindow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.concurrent.ConcurrentHashMap

/**
 * The legacy Timeline window is a per-watch preference.  Keep its durable
 * state separate from the compatibility D-Bus object so a reconnecting watch
 * sees the same window even when org.rockwork is not currently exported.
 */
internal class TimelineWindowCoordinator(
    private val settings: RockpoolSettings,
) : TimelineWindowProvider {
    private val windows = ConcurrentHashMap<String, MutableStateFlow<TimelineWindow>>()
    private val mutableSourceWindow = MutableStateFlow(DEFAULT_WINDOW)

    init {
        reloadPersisted()
    }

    /**
     * Calendar rows have no individual transport while they are created.  Its
     * source query therefore covers the union of the defaults and every
     * address-specific window which has been requested or updated.
     */
    override val sourceWindow: StateFlow<TimelineWindow> = mutableSourceWindow

    @Synchronized
    override fun windowFor(transport: String): StateFlow<TimelineWindow> {
        val address = transport.uppercase()
        val window = windows.computeIfAbsent(address) {
            MutableStateFlow(load(address))
        }
        updateSourceWindow()
        return window
    }

    /** Refresh all durable windows after the legacy-settings importer has run. */
    @Synchronized
    fun reloadPersisted() {
        // Calendar can prepare source rows before a watch reconnects. Include
        // every previously stored address in its union from daemon startup.
        val storedAddresses = settings.entries("").keys
            .mapNotNull { key -> TIMELINE_WINDOW_KEY.matchEntire(key)?.groupValues?.get(1) }
            .map { it.replace('_', ':').uppercase() }
            .distinct()
        // Never replace or discard an already-published flow: a connected BlobDB may be
        // collecting that exact instance.  Reload it to the default when no durable value exists
        // and add any addresses which appeared during migration.
        val addresses = (storedAddresses + windows.keys).distinct()
        addresses.forEach { address ->
            val loaded = load(address)
            windows.computeIfAbsent(address) { MutableStateFlow(loaded) }.value = loaded
        }
        updateSourceWindow()
    }

    /**
     * Normalizes the historic positive-fade spelling and atomically persists
     * all three fields before making the new window visible to BlobDB.
     */
    @Synchronized
    fun update(address: String, start: Int, fade: Int, end: Int): Boolean {
        val window = normalizedTimelineWindow(start, fade, end) ?: return false
        val normalizedAddress = address.uppercase()
        val prefix = keyPrefix(normalizedAddress)
        val values = mapOf(
            "$prefix.start" to window.pastDays.toString(),
            "$prefix.fade" to window.notificationFadeSeconds.toString(),
            "$prefix.end" to window.futureDays.toString(),
        )
        if (!settings.setAllChecked(values)) return false
        windows.computeIfAbsent(normalizedAddress) { MutableStateFlow(window) }.value = window
        updateSourceWindow()
        return true
    }

    private fun load(address: String): TimelineWindow {
        val prefix = keyPrefix(address)
        val keys = listOf("$prefix.start", "$prefix.fade", "$prefix.end")
        // Timeline-window settings are one atomic record. An incomplete record can only come
        // from an interrupted pre-transactional build; never synthesize a hybrid policy from it.
        if (!keys.all(settings::contains)) return DEFAULT_WINDOW
        return normalizedTimelineWindow(
            settings.getInt("$prefix.start", DEFAULT_WINDOW.pastDays),
            settings.getInt("$prefix.fade", DEFAULT_WINDOW.notificationFadeSeconds),
            settings.getInt("$prefix.end", DEFAULT_WINDOW.futureDays),
        ) ?: DEFAULT_WINDOW
    }

    private fun updateSourceWindow() {
        mutableSourceWindow.value = windows.values
            .map { it.value }
            .fold(DEFAULT_WINDOW) { union, window ->
                TimelineWindow(
                    pastDays = minOf(union.pastDays, window.pastDays),
                    notificationFadeSeconds = minOf(
                        union.notificationFadeSeconds,
                        window.notificationFadeSeconds,
                    ),
                    futureDays = maxOf(union.futureDays, window.futureDays),
                )
            }
    }

    private fun keyPrefix(address: String): String =
        "${address.replace(":", "_")}.timeline"

    private companion object {
        val TIMELINE_WINDOW_KEY = Regex(
            "([0-9A-F]{2}(?:_[0-9A-F]{2}){5})\\.timeline\\.(?:start|fade|end)",
            RegexOption.IGNORE_CASE,
        )
        val DEFAULT_WINDOW = TimelineWindow(
            pastDays = -2,
            notificationFadeSeconds = -3600,
            futureDays = 7,
        )
    }
}
