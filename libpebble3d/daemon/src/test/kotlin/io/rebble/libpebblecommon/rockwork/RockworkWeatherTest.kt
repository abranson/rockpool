/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.weather.WeatherLocationData
import io.rebble.libpebblecommon.weather.WeatherType
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.UInt64
import org.freedesktop.dbus.types.Variant
import java.math.BigInteger
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class RockworkWeatherTest {
    @Test
    fun `locations preserve the legacy av of string arrays and stable UUIDs`() {
        val locations = parseRockworkWeatherLocations(
            listOf(
                location("Current Location", "n/a", "n/a"),
                location("London", "51.508530", "-0.125740"),
            ),
        )

        assertEquals("5f63c159-1c67-455f-897e-125d70664c4f", locations[0].key.toString())
        assertEquals("ce06cec5-c548-5e2b-ada7-b4b1d51ab4d2", locations[1].key.toString())
        assertEquals("London", locations[1].name)
        assertEquals("51.508530", locations[1].latitude)

        val settings = encodeRockworkWeatherSettings(locations)
        assertEquals(locations, decodeRockworkWeatherSettings(settings).getOrThrow())
    }

    @Test
    fun `locations reject malformed shapes coordinates duplicates and overflow`() {
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherLocations(listOf(Variant(listOf("London", "1"), "as")))
        }
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherLocations(listOf(Variant(listOf("London", "1", "2"), "av")))
        }
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherLocations(listOf(location("London", "91", "0")))
        }
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherLocations(
                listOf(location("Current", "n/a", "n/a"), location("Current", "1", "2")),
            )
        }
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherLocations((0..6).map { location("Location $it", "0", "0") })
        }
    }

    @Test
    fun `observation maps the legacy fields and validates bounds`() {
        val observation = parseRockworkWeatherObservation(
            conditions = mapOf(
                "text" to Variant("Sunshine"),
                "temperature" to Variant(27),
                "today_hi" to Variant(29.toShort()),
                "today_low" to Variant(16.toShort()),
                "today_icon" to Variant(7.toByte()),
                "tomorrow_icon" to Variant(UInt32(6)),
                "tomorrow_hi" to Variant(25),
                "tomorrow_low" to Variant(12),
                "ts" to Variant("2026-08-12T08:00:00Z"),
            ),
            nowEpochSeconds = { error("explicit timestamp should be used") },
        )

        assertEquals("Sunshine", observation.text)
        assertEquals(27, observation.temperature.toInt())
        assertEquals(WeatherType.Sun, observation.todayIcon)
        assertEquals(WeatherType.Generic, observation.tomorrowIcon)
        assertEquals(1_786_521_600L, observation.timestampEpochSeconds)

        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherObservation(
                mapOf("text" to Variant("bad"), "temperature" to Variant(40_000)),
                nowEpochSeconds = { 1 },
            )
        }
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherObservation(
                mapOf(
                    "text" to Variant("bad"),
                    "temperature" to Variant(1),
                    "today_icon" to Variant(9),
                ),
                nowEpochSeconds = { 1 },
            )
        }
        assertFailsWith<IllegalArgumentException> {
            parseRockworkWeatherObservation(
                mapOf(
                    "text" to Variant("bad"),
                    "temperature" to Variant(1),
                    "ts" to Variant(UInt64(BigInteger.ONE.shiftLeft(63))),
                ),
                nowEpochSeconds = { 1 },
            )
        }
    }

    @Test
    fun `coordinator persists atomically and submits complete ordered snapshots`() {
        var settings = emptyMap<String, String>()
        var permitWrites = true
        val updates = mutableListOf<List<WeatherLocationData>>()
        val coordinator = RockworkWeatherCoordinator(
            loadSettings = { settings },
            replaceSettings = {
                if (permitWrites) settings = it
                permitWrites
            },
            updateWeatherData = updates::add,
            nowEpochSeconds = { 1234 },
        )
        assertEquals(listOf(emptyList()), updates)
        val locations = listOf(
            location("Current Location", "n/a", "n/a"),
            location("London", "51.5", "-0.1"),
        )

        assertTrue(coordinator.setLocations(locations))
        assertEquals(2, updates.last().size)
        assertTrue(updates.last().all { it is WeatherLocationData.WeatherLocationDataFailed })

        assertTrue(coordinator.inject("Current Location", minimalConditions(20, "Clear")))
        val firstInjection = updates.last()
        assertIs<WeatherLocationData.WeatherLocationDataPopulated>(firstInjection[0])
        assertIs<WeatherLocationData.WeatherLocationDataFailed>(firstInjection[1])

        assertTrue(coordinator.inject("London", minimalConditions(15, "Cloudy")))
        val secondInjection = updates.last()
        assertEquals(2, secondInjection.size)
        assertTrue(secondInjection.all { it is WeatherLocationData.WeatherLocationDataPopulated })
        assertEquals(
            listOf("Current Location", "London"),
            secondInjection.map { (it as WeatherLocationData.WeatherLocationDataPopulated).locationName },
        )

        val saved = settings
        permitWrites = false
        assertFalse(coordinator.setLocations(listOf(locations.first())))
        assertEquals(saved, settings)
        assertEquals(2, coordinator.locations().size)
        assertEquals(4, updates.size)
        assertFailsWith<IllegalArgumentException> {
            coordinator.inject("Unknown", minimalConditions(1, "No"))
        }
    }

    @Test
    fun `coordinator replays persisted weather on startup`() {
        val populated = RockworkWeatherLocation(
            key = parseRockworkWeatherLocations(listOf(location("London", "51.5", "-0.1")))
                .single().key,
            name = "London",
            latitude = "51.5",
            longitude = "-0.1",
            observation = RockworkWeatherObservation(
                text = "Cloudy",
                temperature = 15,
                timestampEpochSeconds = 1234,
                todayHigh = 18,
                todayLow = 10,
                todayIcon = WeatherType.CloudyDay,
                tomorrowHigh = 17,
                tomorrowLow = 9,
                tomorrowIcon = WeatherType.LightRain,
            ),
        )
        val updates = mutableListOf<List<WeatherLocationData>>()

        RockworkWeatherCoordinator(
            loadSettings = { encodeRockworkWeatherSettings(listOf(populated)) },
            replaceSettings = { true },
            updateWeatherData = updates::add,
        )

        assertEquals(1, updates.size)
        val replayed = assertIs<WeatherLocationData.WeatherLocationDataPopulated>(
            updates.single().single(),
        )
        assertEquals("London", replayed.locationName)
        assertEquals(15, replayed.currentTemp.toInt())
    }

    private fun location(name: String, latitude: String, longitude: String): Variant<*> =
        Variant(listOf(name, latitude, longitude), "as")

    private fun minimalConditions(temperature: Int, text: String): Map<String, Variant<*>> =
        mapOf("temperature" to Variant(temperature), "text" to Variant(text))
}
