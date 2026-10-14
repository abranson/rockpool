/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import kotlinx.coroutines.runBlocking
import org.freedesktop.dbus.DBusPath
import org.freedesktop.dbus.types.Variant
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RockpoolBatteryTest {
    private val address = "AA:BB:CC:DD:EE:FF"
    private fun watch(level: Int?, connected: Boolean = true, id: String = address) =
        RockpoolBatteryWatch(id, "Pebble", connected, level)

    private fun withSettings(block: (RockpoolSettings) -> Unit) {
        val dir = Files.createTempDirectory("rockpool-battery-test")
        try { block(RockpoolSettings(dir.resolve("settings"))) }
        finally { dir.toFile().deleteRecursively() }
    }

    @Test fun `BlueZ overrides library and invalid values fall back`() = withSettings { settings ->
        runBlocking {
            val changed = mutableListOf<String>()
            val battery = RockpoolBattery(settings, { _, _, _ -> true }, changed::add)
            battery.update(listOf(watch(60)), mapOf(address to 40))
            assertEquals(40, battery.percentage(address.lowercase()))
            battery.update(listOf(watch(60)), mapOf(address to 101))
            assertEquals(60, battery.percentage(address))
            battery.update(listOf(watch(-1)), emptyMap())
            assertEquals(-1, battery.percentage(address))
            assertEquals(listOf(address, address, address), changed)
        }
    }

    @Test fun `boundary and zero warn once until recovery`() = withSettings { settings ->
        runBlocking {
            val warnings = mutableListOf<Int>()
            val battery = RockpoolBattery(settings, { _, _, level -> warnings.add(level); true }, {})
            for (level in listOf(20, 11, 10, 10, 9, 0, 15, 0)) {
                battery.update(listOf(watch(level)), emptyMap())
            }
            assertEquals(listOf(10, 0), warnings)
        }
    }

    @Test fun `unknown and disconnected readings do not warn or rearm`() = withSettings { settings ->
        runBlocking {
            val warnings = mutableListOf<Int>()
            val battery = RockpoolBattery(settings, { _, _, level -> warnings.add(level); true }, {})
            battery.update(listOf(watch(null)), emptyMap())
            battery.update(listOf(watch(5, false)), mapOf(address to 5))
            assertEquals(-1, battery.percentage(address))
            assertTrue(warnings.isEmpty())
            battery.update(listOf(watch(5)), emptyMap())
            battery.update(listOf(watch(null)), emptyMap())
            battery.update(listOf(watch(50, false)), mapOf(address to 50))
            battery.update(listOf(watch(5)), emptyMap())
            assertEquals(listOf(5), warnings)
        }
    }

    @Test fun `warning latch survives service restart`() = withSettings { settings ->
        runBlocking {
            var warnings = 0
            val notify: suspend (String, String, Int) -> Boolean = { _, _, _ -> warnings++; true }
            RockpoolBattery(settings, notify, {}).update(listOf(watch(10)), emptyMap())
            RockpoolBattery(settings, notify, {}).update(listOf(watch(9)), emptyMap())
            assertEquals(1, warnings)
        }
    }

    @Test fun `threshold is persistent per watch and off disables notifications`() = withSettings { settings ->
        runBlocking {
            val warnings = mutableListOf<Int>()
            val battery = RockpoolBattery(settings, { _, _, level -> warnings.add(level); true }, {})
            assertEquals(10, battery.threshold(address))
            assertTrue(battery.setThreshold(address, 0))
            battery.update(listOf(watch(0)), emptyMap())
            assertTrue(warnings.isEmpty())
            assertTrue(battery.setThreshold(address, 15))
            battery.update(listOf(watch(16)), emptyMap())
            battery.update(listOf(watch(15)), emptyMap())
            assertEquals(listOf(15), warnings)
            assertEquals(15, RockpoolBattery(settings, { _, _, _ -> true }, {}).threshold(address))
            assertEquals(10, battery.threshold("11:22:33:44:55:66"))
            assertFalse(battery.setThreshold(address, -1))
            assertFalse(battery.setThreshold(address, 101))
            assertEquals(15, battery.threshold(address))
        }
    }

    @Test fun `two watches warn independently and failed delivery retries`() = withSettings { settings ->
        runBlocking {
            var attempts = 0
            val battery = RockpoolBattery(settings, { _, _, _ -> ++attempts > 1 }, {})
            val watches = listOf(watch(10), watch(10, id = "11:22:33:44:55:66"))
            battery.update(watches, emptyMap())
            assertEquals(2, attempts)
            battery.update(watches, emptyMap())
            assertEquals(3, attempts)
            battery.update(watches, emptyMap())
            assertEquals(3, attempts)
        }
    }

    @Test fun `failed threshold persistence keeps previous value and revision`() {
        val dir = Files.createTempDirectory("rockpool-battery-unwritable")
        try {
            val parent = dir.resolve("not-a-directory")
            Files.write(parent, byteArrayOf(0))
            val battery = RockpoolBattery(RockpoolSettings(parent.resolve("settings")), { _, _, _ -> true }, {})
            assertFalse(battery.setThreshold(address, 5))
            assertEquals(10, battery.threshold(address))
            assertEquals(0L, battery.settingsRevision.value)
        } finally { dir.toFile().deleteRecursively() }
    }

    @Test fun `lowering threshold or disabling warnings withdraws and rearms`() = withSettings { settings ->
        runBlocking {
            var warnings = 0
            val closed = mutableListOf<String>()
            val battery = RockpoolBattery(settings, { _, _, _ -> warnings++; true }, {},
                { closed.add(it); true })
            battery.update(listOf(watch(8)), emptyMap())
            battery.setThreshold(address, 5)
            battery.update(listOf(watch(8)), emptyMap())
            assertEquals(listOf(address), closed)
            battery.setThreshold(address, 10)
            battery.update(listOf(watch(8)), emptyMap())
            assertEquals(2, warnings)
            battery.setThreshold(address, 0)
            battery.update(listOf(watch(null)), emptyMap())
            assertEquals(listOf(address, address), closed)
        }
    }

    @Test fun `restarted observer withdraws persisted warning on recovery`() = withSettings { settings ->
        runBlocking {
            RockpoolBattery(settings, { _, _, _ -> true }, {}).update(listOf(watch(8)), emptyMap())
            val closed = mutableListOf<String>()
            val restarted = RockpoolBattery(settings, { _, _, _ -> true }, {}, { closed.add(it); true })
            restarted.update(listOf(watch(30)), emptyMap())
            assertEquals(listOf(address), closed)
        }
    }

    @Test fun `failed withdrawal retries without rearming early`() = withSettings { settings ->
        runBlocking {
            var attempts = 0
            var warnings = 0
            val battery = RockpoolBattery(settings, { _, _, _ -> warnings++; true }, {}, { ++attempts > 1 })
            battery.update(listOf(watch(8)), emptyMap())
            battery.update(listOf(watch(20)), emptyMap())
            battery.update(listOf(watch(8)), emptyMap())
            assertEquals(1, warnings)
            battery.update(listOf(watch(20)), emptyMap())
            battery.update(listOf(watch(8)), emptyMap())
            assertEquals(2, warnings)
            assertEquals(2, attempts)
        }
    }

    @Test fun `BlueZ snapshot selects connected devices on selected adapter`() {
        fun device(connected: Boolean, level: Int) = mapOf(
            "org.bluez.Device1" to mapOf("Address" to Variant(address.lowercase()), "Connected" to Variant(connected)),
            "org.bluez.Battery1" to mapOf("Percentage" to Variant(level.toByte())),
        )
        val path = DBusPath("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF")
        assertEquals(mapOf(address to 0), bluezBatteryPercentages(mapOf(path to device(true, 0)), "/org/bluez/hci0"))
        assertTrue(bluezBatteryPercentages(mapOf(path to device(false, 20)), "/org/bluez/hci0").isEmpty())
        assertTrue(bluezBatteryPercentages(mapOf(path to device(true, 101)), "/org/bluez/hci0").isEmpty())
        assertTrue(bluezBatteryPercentages(mapOf(path to device(true, 20)), "/org/bluez/hci1").isEmpty())
    }
}
