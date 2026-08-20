/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import org.freedesktop.dbus.types.Variant
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNull

class RockworkSignalContractTest {
    @Test
    fun `installed applications change has no payload`() {
        val signal = RockworkPebble.InstalledAppsChanged(WATCH_PATH)

        assertEquals(WATCH_PATH, signal.path)
        assertEquals("InstalledAppsChanged", signal.name)
        assertNull(signal.parameters)
    }

    @Test
    fun `health data change retains legacy empty payload`() {
        val signal = RockworkPebble.HealthDataChanged(WATCH_PATH)

        assertEquals(WATCH_PATH, signal.path)
        assertEquals("HealthDataChanged", signal.name)
        assertNull(signal.parameters)
    }

    @Test
    fun `health history methods retain legacy D-Bus shapes`() {
        val overview = RockworkPebble::class.java.getMethod("HealthOverview")
        val fetch = RockworkPebble::class.java.getMethod("FetchHealthData")

        assertEquals(Map::class.java, overview.returnType)
        assertEquals(Void.TYPE, fetch.returnType)
        assertEquals(0, overview.parameterCount)
        assertEquals(0, fetch.parameterCount)
    }

    @Test
    fun `weather locations change retains legacy variant array payload`() {
        val locations = listOf(
            Variant(listOf("Current Location", "n/a", "n/a"), "as"),
            Variant(listOf("London", "51.5", "-0.1"), "as"),
        )

        val signal = RockworkPebble.WeatherLocationsChanged(WATCH_PATH, locations)

        assertEquals(WATCH_PATH, signal.path)
        assertEquals("WeatherLocationsChanged", signal.name)
        assertEquals("av", signal.sig)
        assertContentEquals(arrayOf(locations), signal.parameters)
        assertEquals(listOf("as", "as"), locations.map { it.sig })
    }

    private companion object {
        const val WATCH_PATH = "/org/rockwork/AA_BB_CC_DD_EE_FF"
    }
}
