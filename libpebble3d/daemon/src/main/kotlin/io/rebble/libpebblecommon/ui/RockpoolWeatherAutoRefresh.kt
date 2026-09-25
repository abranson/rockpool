/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.linux.weather.OpenMeteoForecast
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.time.Duration.Companion.minutes

/**
 * Runs one immediate refresh, then refreshes periodically or when settings change. Network
 * failures leave the last durable observation untouched; triggers are conflated.
 */
internal class RockpoolWeatherAutoRefresh(
    private val scope: CoroutineScope,
    private val coordinator: RockpoolWeatherCoordinator,
    private val units: suspend () -> String,
    private val fetch: suspend (
        RockpoolWeatherFetchTarget,
        RockpoolWeatherCoordinates,
        String,
    ) -> RockpoolWeatherObservation?,
    private val resolveCurrentLocation: suspend () -> RockpoolWeatherCoordinates? = { null },
    private val resolveCurrentLocationName: suspend (RockpoolWeatherCoordinates) -> String? = { null },
    private val refreshIntervalMillis: Long = DEFAULT_REFRESH_INTERVAL.inWholeMilliseconds,
    private val onFailure: (Throwable) -> Unit = {},
) {
    private val requests = Channel<Unit>(Channel.CONFLATED)
    private val started = AtomicBoolean(false)

    fun start() {
        if (!started.compareAndSet(false, true)) return
        scope.launch {
            refreshOnce()
            while (currentCoroutineContext().isActive) {
                withTimeoutOrNull(refreshIntervalMillis) { requests.receive() }
                refreshOnce()
            }
        }
    }

    fun trigger() {
        requests.trySend(Unit)
    }

    internal suspend fun refreshOnce() {
        val selectedUnits = try {
            units().takeIf { it in setOf("m", "e", "h") } ?: "m"
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            onFailure(e)
            return
        }
        if (!coordinator.setImperialUnits(selectedUnits == "e")) return
        coordinator.automaticFetchTargets().forEach { target ->
            try {
                val coordinates = if (target.currentLocation) {
                    resolveCurrentLocation()
                } else {
                    target.coordinates
                }?.takeIf(::validCoordinates) ?: return@forEach
                val resolvedName = if (target.currentLocation) {
                    resolveCurrentLocationName(coordinates)
                } else {
                    null
                }
                fetch(target, coordinates, selectedUnits)?.let { observation ->
                    if ((units() == "e") == (selectedUnits == "e")) {
                        coordinator.applyAutomaticObservation(target, observation, resolvedName)
                    }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                onFailure(e)
            }
        }
    }
}

private fun validCoordinates(coordinates: RockpoolWeatherCoordinates): Boolean =
    coordinates.latitude.isFinite() && coordinates.longitude.isFinite() &&
        coordinates.latitude in -90.0..90.0 && coordinates.longitude in -180.0..180.0

internal fun OpenMeteoForecast.toRockpoolWeatherObservation() = RockpoolWeatherObservation(
    text = text,
    temperature = temperature,
    timestampEpochSeconds = timestampEpochSeconds,
    todayHigh = todayHigh,
    todayLow = todayLow,
    todayIcon = todayIcon,
    tomorrowHigh = tomorrowHigh,
    tomorrowLow = tomorrowLow,
    tomorrowIcon = tomorrowIcon,
    source = RockpoolWeatherObservationSource.AUTOMATIC,
)

private val DEFAULT_REFRESH_INTERVAL = 30.minutes
