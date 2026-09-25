/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.WatchPrefs
import io.rebble.libpebblecommon.database.dao.WatchPreference
import io.rebble.libpebblecommon.database.entity.BoolWatchPref
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.async
import kotlinx.coroutines.yield
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RockpoolQuietTimeTest {
    @Test
    fun `snapshot uses library defaults and includes watch-side changes`() {
        val defaults = quietTimeSnapshot(emptyList())
        assertEquals("0", defaults["dndManuallyEnabled"])
        assertEquals("00:00-06:00", defaults["dndWeekdaySchedule"])
        val changed = quietTimeSnapshot(listOf(WatchPreference(BoolWatchPref.QuietTimeManuallyEnabled, true)))
        assertEquals("1", changed["dndManuallyEnabled"])
        assertEquals(defaults - "dndManuallyEnabled", changed - "dndManuallyEnabled")
    }

    @Test
    fun `reject malformed values and unrelated watch settings`() {
        listOf("24:00-06:00", "22:60-07:00", "-1:00-07:00", "22:00", "garbage").forEach {
            assertNull(quietTimeUpdate("dndWeekdaySchedule", it))
        }
        assertNull(quietTimeUpdate("dndManuallyEnabled", "true"))
        assertNull(quietTimeUpdate("dndInterruptionsMask", "15"))
        assertNull(quietTimeUpdate("dndShowNotifications", "2"))
        assertNull(quietTimeUpdate("clock24h", "1"))
    }

    @Test
    fun `save waits for database observation`() = runBlocking {
        val prefs = object : WatchPrefs {
            override val watchPrefs = MutableStateFlow<List<WatchPreference<*>>>(emptyList())
            var pending: WatchPreference<*>? = null
            override fun setWatchPref(watchPref: WatchPreference<*>) { pending = watchPref }
        }
        val save = async { saveQuietTime(prefs, "dndManuallyEnabled", "1") }
        yield()
        assertFalse(save.isCompleted)
        prefs.watchPrefs.value = listOf(requireNotNull(prefs.pending))
        assertTrue(save.await())
    }

    @Test
    fun `overnight schedules and individual writes preserve other preferences`() = runBlocking {
        val prefs = object : WatchPrefs {
            override val watchPrefs = MutableStateFlow<List<WatchPreference<*>>>(
                listOf(WatchPreference(BoolWatchPref.QuietTimeManuallyEnabled, true))
            )
            override fun setWatchPref(watchPref: WatchPreference<*>) {
                watchPrefs.value = watchPrefs.value.filterNot { it.pref == watchPref.pref } + watchPref
            }
        }
        assertTrue(saveQuietTime(prefs, "dndWeekdaySchedule", "22:30-07:15"))
        assertEquals("22:30-07:15", quietTimeSnapshot(prefs.watchPrefs.value)["dndWeekdaySchedule"])
        assertEquals("1", quietTimeSnapshot(prefs.watchPrefs.value)["dndManuallyEnabled"])
        assertFalse(saveQuietTime(prefs, "dndWeekendSchedule", "25:00-07:00"))
        assertEquals(2, prefs.watchPrefs.value.size)
    }
}
