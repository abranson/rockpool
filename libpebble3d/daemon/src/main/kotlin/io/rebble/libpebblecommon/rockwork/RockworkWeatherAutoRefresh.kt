/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

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
internal class RockworkWeatherAutoRefresh(
    private val scope: CoroutineScope,
    private val coordinator: RockworkWeatherCoordinator,
    private val units: () -> String,
    private val fetch: suspend (
        RockworkWeatherFetchTarget,
        String,
    ) -> RockworkWeatherObservation?,
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
        val selectedUnits = units().takeIf { it in setOf("m", "e", "h") } ?: "m"
        coordinator.automaticFetchTargets().forEach { target ->
            try {
                fetch(target, selectedUnits)?.let { observation ->
                    coordinator.applyAutomaticObservation(target, observation)
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                onFailure(e)
            }
        }
    }
}

internal fun OpenMeteoForecast.toRockworkWeatherObservation() = RockworkWeatherObservation(
    text = text,
    temperature = temperature,
    timestampEpochSeconds = timestampEpochSeconds,
    todayHigh = todayHigh,
    todayLow = todayLow,
    todayIcon = todayIcon,
    tomorrowHigh = tomorrowHigh,
    tomorrowLow = tomorrowLow,
    tomorrowIcon = tomorrowIcon,
    source = RockworkWeatherObservationSource.AUTOMATIC,
)

private val DEFAULT_REFRESH_INTERVAL = 30.minutes
