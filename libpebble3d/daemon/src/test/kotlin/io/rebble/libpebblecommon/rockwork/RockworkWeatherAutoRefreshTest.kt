/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.weather.WeatherLocationData
import io.rebble.libpebblecommon.weather.WeatherType
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.freedesktop.dbus.types.Variant
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RockworkWeatherAutoRefreshTest {
    @Test
    fun `automatic observation persists and replays as automatic`() {
        var settings = emptyMap<String, String>()
        val updates = mutableListOf<List<WeatherLocationData>>()
        val coordinator = coordinator(
            settings = { settings },
            replace = { settings = it },
            updates = updates,
        )
        assertTrue(coordinator.setLocations(listOf(location("London", "51.5", "-0.1"))))

        val target = coordinator.automaticFetchTargets().single()
        assertTrue(coordinator.applyAutomaticObservation(target, observation("Automatic", 14)))
        assertTrue(settings.values.contains("automatic"))
        assertEquals(
            RockworkWeatherObservationSource.AUTOMATIC,
            decodeRockworkWeatherSettings(settings).getOrThrow().single().observation?.source,
        )

        val replayed = mutableListOf<List<WeatherLocationData>>()
        RockworkWeatherCoordinator(
            loadSettings = { settings },
            replaceSettings = { true },
            updateWeatherData = replayed::add,
        )
        val data = assertIs<WeatherLocationData.WeatherLocationDataPopulated>(replayed.single().single())
        assertEquals("London", data.locationName)
        assertEquals(14, data.currentTemp.toInt())
    }

    @Test
    fun `external injection wins over an in-flight automatic result`() {
        var settings = emptyMap<String, String>()
        val coordinator = coordinator(settings = { settings }, replace = { settings = it })
        assertTrue(coordinator.setLocations(listOf(location("London", "51.5", "-0.1"))))
        val target = coordinator.automaticFetchTargets().single()

        assertTrue(coordinator.inject("London", conditions(9, "External")))
        assertFalse(coordinator.applyAutomaticObservation(target, observation("Automatic", 20)))
        val stored = decodeRockworkWeatherSettings(settings).getOrThrow().single().observation
        assertEquals("External", stored?.text)
        assertEquals(RockworkWeatherObservationSource.EXTERNAL, stored?.source)
    }

    @Test
    fun `coordinate change removes a prior automatic observation`() {
        var settings = emptyMap<String, String>()
        val coordinator = coordinator(settings = { settings }, replace = { settings = it })
        assertTrue(coordinator.setLocations(listOf(location("London", "51.5", "-0.1"))))
        assertTrue(
            coordinator.applyAutomaticObservation(
                coordinator.automaticFetchTargets().single(), observation("Automatic", 14),
            ),
        )

        assertTrue(coordinator.setLocations(listOf(location("London", "52.0", "-0.1"))))
        val stored = decodeRockworkWeatherSettings(settings).getOrThrow().single()
        assertNull(stored.observation)
        assertEquals("52.0", coordinator.automaticFetchTargets().single().latitude)
    }

    @Test
    fun `automatic targets include current placeholder and skip external observations`() {
        val coordinator = coordinator()
        assertTrue(
            coordinator.setLocations(
                listOf(
                    location("Current", "n/a", "n/a"),
                    location("London", "51.5", "-0.1"),
                ),
            ),
        )
        assertTrue(coordinator.inject("London", conditions(12, "External")))

        val target = coordinator.automaticFetchTargets().single()
        assertTrue(target.currentLocation)
        assertNull(target.coordinates)
        assertEquals("n/a", target.latitude)
        assertEquals("n/a", target.longitude)
    }

    @Test
    fun `automatic refresh failures preserve the last durable observation`() = runBlocking {
        var settings = emptyMap<String, String>()
        val updates = mutableListOf<List<WeatherLocationData>>()
        val coordinator = coordinator(
            settings = { settings },
            replace = { settings = it },
            updates = updates,
        )
        assertTrue(coordinator.setLocations(listOf(location("London", "51.5", "-0.1"))))
        val target = coordinator.automaticFetchTargets().single()
        assertTrue(coordinator.applyAutomaticObservation(target, observation("Before", 11)))
        val saved = settings
        val updateCount = updates.size
        var failures = 0
        val refresh = RockworkWeatherAutoRefresh(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            coordinator = coordinator,
            units = { "m" },
            fetch = { _, _, _ -> error("network down") },
            onFailure = { failures++ },
        )

        refresh.refreshOnce()

        assertEquals(1, failures)
        assertEquals(saved, settings)
        assertEquals(updateCount, updates.size)
    }

    @Test
    fun `automatic refresh performs an immediate fetch and a deterministic trigger`() = runBlocking {
        val coordinator = coordinator()
        assertTrue(coordinator.setLocations(listOf(location("London", "51.5", "-0.1"))))
        var calls = 0
        val job = Job()
        val refresh = RockworkWeatherAutoRefresh(
            scope = CoroutineScope(job + Dispatchers.Unconfined),
            coordinator = coordinator,
            units = { "h" },
            fetch = { _, _, units ->
                assertEquals("h", units)
                calls++
                observation("Automatic $calls", calls)
            },
            refreshIntervalMillis = Long.MAX_VALUE,
        )

        refresh.start()
        assertEquals(1, calls)
        refresh.trigger()
        yield()
        assertEquals(2, calls)
        job.cancel()
    }

    @Test
    fun `current location resolves only for fetch and retains canonical coordinates`() = runBlocking {
        var settings = emptyMap<String, String>()
        val coordinator = coordinator(settings = { settings }, replace = { settings = it })
        assertTrue(coordinator.setLocations(listOf(location("Current", "n/a", "n/a"))))
        var fetched: RockworkWeatherCoordinates? = null
        val refresh = RockworkWeatherAutoRefresh(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            coordinator = coordinator,
            units = { "m" },
            fetch = { target, coordinates, _ ->
                assertTrue(target.currentLocation)
                fetched = coordinates
                observation("Automatic", 14)
            },
            resolveCurrentLocation = { RockworkWeatherCoordinates(51.5, -0.1) },
        )

        refresh.refreshOnce()

        assertEquals(RockworkWeatherCoordinates(51.5, -0.1), fetched)
        val stored = decodeRockworkWeatherSettings(settings).getOrThrow().single()
        assertEquals("n/a", stored.latitude)
        assertEquals("n/a", stored.longitude)
        assertEquals(RockworkWeatherObservationSource.AUTOMATIC, stored.observation?.source)
    }

    @Test
    fun `current location errors and invalid coordinates retain prior observation`() = runBlocking {
        var settings = emptyMap<String, String>()
        val coordinator = coordinator(settings = { settings }, replace = { settings = it })
        assertTrue(coordinator.setLocations(listOf(location("Current", "n/a", "n/a"))))
        val target = coordinator.automaticFetchTargets().single()
        assertTrue(coordinator.applyAutomaticObservation(target, observation("Before", 11)))
        val saved = settings
        var fetches = 0
        val refresh = RockworkWeatherAutoRefresh(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            coordinator = coordinator,
            units = { "m" },
            fetch = { _, _, _ -> fetches++; observation("After", 20) },
            resolveCurrentLocation = { RockworkWeatherCoordinates(Double.NaN, 0.0) },
        )

        refresh.refreshOnce()

        assertEquals(0, fetches)
        assertEquals(saved, settings)
    }

    @Test
    fun `external current injection rejects in flight automatic result`() = runBlocking {
        var settings = emptyMap<String, String>()
        val coordinator = coordinator(settings = { settings }, replace = { settings = it })
        assertTrue(coordinator.setLocations(listOf(location("Current", "n/a", "n/a"))))
        val refresh = RockworkWeatherAutoRefresh(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            coordinator = coordinator,
            units = { "m" },
            fetch = { _, _, _ ->
                assertTrue(coordinator.inject("Current", conditions(9, "External")))
                observation("Automatic", 20)
            },
            resolveCurrentLocation = { RockworkWeatherCoordinates(51.5, -0.1) },
        )

        refresh.refreshOnce()

        val stored = decodeRockworkWeatherSettings(settings).getOrThrow().single().observation
        assertEquals("External", stored?.text)
        assertEquals(RockworkWeatherObservationSource.EXTERNAL, stored?.source)
    }

    private fun coordinator(
        settings: () -> Map<String, String> = { emptyMap() },
        replace: (Map<String, String>) -> Unit = {},
        updates: MutableList<List<WeatherLocationData>> = mutableListOf(),
    ): RockworkWeatherCoordinator = RockworkWeatherCoordinator(
        loadSettings = settings,
        replaceSettings = { replacement -> replace(replacement); true },
        updateWeatherData = updates::add,
        nowEpochSeconds = { 1234 },
    )

    private fun observation(text: String, temperature: Int) = RockworkWeatherObservation(
        text = text,
        temperature = temperature.toShort(),
        timestampEpochSeconds = 1234,
        todayHigh = temperature.toShort(),
        todayLow = temperature.toShort(),
        todayIcon = WeatherType.Sun,
        tomorrowHigh = temperature.toShort(),
        tomorrowLow = temperature.toShort(),
        tomorrowIcon = WeatherType.Sun,
    )

    private fun location(name: String, latitude: String, longitude: String): Variant<*> =
        Variant(listOf(name, latitude, longitude), "as")

    private fun conditions(temperature: Int, text: String): Map<String, Variant<*>> =
        mapOf("temperature" to Variant(temperature), "text" to Variant(text))
}
