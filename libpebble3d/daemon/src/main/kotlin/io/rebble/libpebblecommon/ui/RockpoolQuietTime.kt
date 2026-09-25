/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.WatchPrefs
import io.rebble.libpebblecommon.database.dao.WatchPreference
import io.rebble.libpebblecommon.database.entity.BoolWatchPref
import io.rebble.libpebblecommon.database.entity.EnumWatchPref
import io.rebble.libpebblecommon.database.entity.QuietTimeSchedule
import io.rebble.libpebblecommon.database.entity.ScheduleWatchPref
import io.rebble.libpebblecommon.database.entity.WatchPref
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withTimeout

internal val quietTimePrefs: List<WatchPref<*>> = listOf(
    BoolWatchPref.QuietTimeManuallyEnabled,
    BoolWatchPref.CalendarAwareQuietTime,
    BoolWatchPref.QuietTimeWeekdayScheduleEnabled,
    BoolWatchPref.QuietTimeWeekendScheduleEnabled,
    BoolWatchPref.QuietTimeMotionBacklight,
    BoolWatchPref.QuietTimeAutoDismiss,
    ScheduleWatchPref.QuietTimeWeekdaySchedule,
    ScheduleWatchPref.QuietTimeWeekendSchedule,
    EnumWatchPref.QuietTimeInterruptions,
    EnumWatchPref.QuietTimeShowNotifications,
)

private fun <T> encoded(pref: WatchPref<T>, values: List<WatchPreference<*>>): String {
    val value = values.firstOrNull { it.pref == pref }
    return pref.encodeValue(value?.let { pref.castParent(it).valueOrDefault() } ?: pref.defaultValue)
}

internal fun quietTimeSnapshot(values: List<WatchPreference<*>>): Map<String, String> =
    quietTimePrefs.associate { it.id to encoded(it, values) }

internal fun quietTimeUpdate(key: String, value: String): WatchPreference<*>? =
    when (val pref = quietTimePrefs.firstOrNull { it.id == key }) {
        is BoolWatchPref -> if (value == "0" || value == "1") {
            WatchPreference(pref, value == "1")
        } else null
        is ScheduleWatchPref -> QuietTimeSchedule.parse(value)?.let { WatchPreference(pref, it) }
        is EnumWatchPref -> pref.options.firstOrNull { it.code.toString() == value }
            ?.let { WatchPreference(pref, it) }
        else -> null
    }

// The library setter launches a database write. Wait for the persisted value rather than
// acknowledging just the launch, so the UI cannot immediately read the old preference back.
internal suspend fun saveQuietTime(prefs: WatchPrefs, key: String, value: String): Boolean {
    val update = quietTimeUpdate(key, value) ?: return false
    val expected = encoded(update.pref, listOf(update))
    withTimeout(5_000) {
        prefs.setWatchPref(update)
        prefs.watchPrefs.first { quietTimeSnapshot(it)[key] == expected }
    }
    return true
}
