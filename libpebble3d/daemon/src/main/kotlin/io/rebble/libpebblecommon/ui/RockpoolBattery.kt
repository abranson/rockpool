/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

internal data class RockpoolBatteryWatch(
    val address: String,
    val name: String,
    val connected: Boolean,
    val libraryPercentage: Int?,
)

internal fun validBatteryPercentage(value: Int?): Int? = value?.takeIf { it in 0..100 }

/** Serialized by the battery observer; unknown/disconnected readings never re-arm a warning. */
internal class RockpoolBattery(
    private val settings: RockpoolSettings,
    private val notify: suspend (address: String, name: String, percentage: Int) -> Boolean,
    private val changed: (address: String) -> Unit,
    private val dismiss: suspend (address: String) -> Boolean = { true },
) {
    private val logger = Logger.withTag("RockpoolBattery")
    private val revisions = MutableStateFlow(0L)
    val settingsRevision = revisions.asStateFlow()
    @Volatile private var levels: Map<String, Int> = emptyMap()
    private val warned = mutableMapOf<String, Boolean>()

    fun percentage(address: String): Int = levels[address.uppercase()] ?: -1

    fun threshold(address: String): Int = settings.getInt(key(address, "threshold"), 10)
        .takeIf { it in 0..100 } ?: 10

    @Synchronized
    fun setThreshold(address: String, value: Int): Boolean {
        if (value !in 0..100 || !settings.setChecked(key(address, "threshold"), value.toString())) {
            return false
        }
        revisions.value += 1
        return true
    }

    suspend fun update(watches: List<RockpoolBatteryWatch>, bluez: Map<String, Int>) {
        val next = watches.associate { watch ->
            watch.address.uppercase() to if (watch.connected) {
                validBatteryPercentage(bluez[watch.address.uppercase()])
                    ?: validBatteryPercentage(watch.libraryPercentage) ?: -1
            } else -1
        }
        val previous = levels
        levels = next
        (previous.keys + next.keys).filter { previous[it] != next[it] }.forEach(changed)
        watches.forEach { watch ->
            val address = watch.address.uppercase()
            val level = next[address] ?: -1
            val threshold = threshold(address)
            val wasWarned = warned.getOrPut(address) {
                settings.getBool(key(address, "warned"))
            }
            if (threshold == 0 || level > threshold) {
                if (dismiss(address) && wasWarned) markWarned(address, false)
            } else if (level >= 0 && !wasWarned && notify(address, watch.name, level)) {
                markWarned(address, true)
            }
        }
    }

    private fun markWarned(address: String, value: Boolean) {
        warned[address] = value
        if (!settings.setChecked(key(address, "warned"), value.toString())) {
            logger.w { "could not persist low-battery warning state" }
        }
    }

    private fun key(address: String, suffix: String) =
        "${address.uppercase().replace(':', '_')}.battery.$suffix"
}
