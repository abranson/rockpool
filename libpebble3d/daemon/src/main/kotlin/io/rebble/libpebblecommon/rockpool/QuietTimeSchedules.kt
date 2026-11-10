/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.WatchPrefs
import io.rebble.libpebblecommon.ui.quietTimeSnapshot
import io.rebble.libpebblecommon.ui.saveQuietTime
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlin.time.Duration.Companion.seconds

/** Coordinates the two schedule flags while keeping their selected days durable. */
internal class QuietTimeSchedules(
    private val settings: RockpoolSettings,
    private val prefs: WatchPrefs,
) {
    private val logger = Logger.withTag("QuietTimeSchedules")
    private val mutex = Mutex()
    private var inFlight: Pair<String, String>? = null

    fun start(scope: CoroutineScope) {
        scope.launch { prefs.watchPrefs.collect { reconcile() } }
    }

    suspend fun setEnabled(value: Boolean): Boolean = guarded {
        if (!resumePending()) return@guarded false
        val current = snapshot()
        if (value == (current != "00")) return@guarded true
        val target = if (value) {
            settings.get(SAVED).takeIf { validPair(it) && it != "00" } ?: "11"
        } else {
            "00"
        }
        queue(target, if (value) target else current)
    }

    suspend fun setDayEnabled(key: String, value: String): Boolean = guarded {
        if ((key != WEEKDAY && key != WEEKEND) || (value != "0" && value != "1")) return@guarded false
        if (!resumePending()) return@guarded false
        val current = snapshot()
        val target = current.toCharArray()
        target[if (key == WEEKDAY) 0 else 1] = value[0]
        val updated = target.concatToString()
        if (updated == current) return@guarded true
        val selection = if (updated != "00") updated else current
        queue(updated, selection)
    }

    suspend fun reconcile(): Boolean = guarded { resumePending() }

    private suspend fun queue(target: String, selection: String): Boolean {
        // Save the selection and recovery target together before either asynchronous write.
        if (!settings.setAllChecked(mapOf(SAVED to selection, PENDING to target))) return false
        return resumePending()
    }

    private suspend fun resumePending(): Boolean {
        val target = settings.get(PENDING)
        return if (validPair(target)) apply(target) else true
    }

    private suspend fun apply(target: String): Boolean {
        if (!validPair(target)) return false
        for ((index, key) in listOf(WEEKDAY, WEEKEND).withIndex()) {
            val desired = target[index].toString()
            val current = snapshot()
            inFlight?.let { (pendingKey, pendingValue) ->
                // A timed-out library write can still finish. Do not launch a duplicate
                // or an opposite command until its persisted value has been observed.
                if (current[indexOf(pendingKey)].toString() != pendingValue) return false
                inFlight = null
            }
            if (current[index].toString() == desired) continue
            try {
                if (!saveQuietTime(prefs, key, desired)) return false
            } catch (e: TimeoutCancellationException) {
                inFlight = key to desired
                throw e
            }
        }
        if (snapshot() != target) return false
        return settings.replacePrefix(PENDING, emptyMap())
    }

    private suspend fun snapshot(): String = withTimeout(5.seconds) {
        val values = quietTimeSnapshot(prefs.watchPrefs.first())
        (values[WEEKDAY] ?: "0") + (values[WEEKEND] ?: "0")
    }

    private suspend fun guarded(block: suspend () -> Boolean): Boolean = mutex.withLock {
        try { block() } catch (e: CancellationException) {
            if (e !is TimeoutCancellationException) throw e
            logger.w { "Quiet Time schedule persistence timed out; retaining pending state" }
            false
        } catch (e: Exception) {
            logger.w { "Quiet Time schedule update failed: ${e.message}" }
            false
        }
    }

    private fun validPair(value: String): Boolean = value.length == 2 && value.all { it == '0' || it == '1' }
    private fun indexOf(key: String): Int = if (key == WEEKDAY) 0 else 1

    private companion object {
        const val WEEKDAY = "dndWeekdayScheduleEnabled"
        const val WEEKEND = "dndWeekendScheduleEnabled"
        const val SAVED = "quietTime.schedulesSavedSelection"
        const val PENDING = "quietTime.schedulesPendingTarget"
    }
}
