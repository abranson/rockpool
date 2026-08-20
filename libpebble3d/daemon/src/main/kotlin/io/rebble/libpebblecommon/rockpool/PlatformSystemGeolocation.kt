/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.util.GeolocationPositionResult
import io.rebble.libpebblecommon.util.SystemGeolocation
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlin.math.max
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Instant

/** Sailfish geolocation backed by the generation-gated native platform provider. */
internal class PlatformSystemGeolocation(
    private val queryLocation: suspend (Boolean, Duration) -> PlatformLocationQueryResult,
    private val monotonicMillis: () -> Long = { System.nanoTime() / 1_000_000L },
) : SystemGeolocation {
    private val cacheLock = Mutex()
    private var cached: GeolocationPositionResult.Success? = null
    private var cachedAtMillis = 0L

    override suspend fun getCurrentPosition(
        maximumAge: Duration?,
        timeout: Duration?,
        highAccuracy: Boolean,
    ): GeolocationPositionResult {
        val effectiveMaximumAge = maximumAge ?: SystemGeolocation.DEFAULT_MAX_AGE
        cachedIfFresh(effectiveMaximumAge)?.let { return it }

        val effectiveTimeout = (timeout ?: SystemGeolocation.DEFAULT_TIMEOUT)
            .inWholeMilliseconds
            .coerceIn(1L, PlatformProviderController.LOCATION_TIMEOUT_MAX_MS.toLong())
            .milliseconds
        val result = try {
            queryLocation(highAccuracy, effectiveTimeout)
        } catch (e: CancellationException) {
            throw e
        }
        return when (result) {
            is PlatformLocationQueryResult.Success -> result.location.toGeolocation().also {
                cacheLock.withLock {
                    cached = it
                    cachedAtMillis = monotonicMillis()
                }
            }
            is PlatformLocationQueryResult.Error -> cachedValue()
                ?: GeolocationPositionResult.Error(locationError(result.status))
        }
    }

    override suspend fun watchPosition(
        interval: Duration,
        highAccuracy: Boolean,
    ): Flow<GeolocationPositionResult> = flow {
        val effectiveInterval = max(interval.inWholeMilliseconds, MIN_WATCH_INTERVAL_MS).milliseconds
        while (true) {
            emit(
                getCurrentPosition(
                    maximumAge = Duration.ZERO,
                    timeout = SystemGeolocation.DEFAULT_TIMEOUT,
                    highAccuracy = highAccuracy,
                ),
            )
            delay(effectiveInterval)
        }
    }

    private suspend fun cachedIfFresh(maximumAge: Duration): GeolocationPositionResult.Success? =
        cacheLock.withLock {
            val value = cached ?: return@withLock null
            val ageMillis = max(0L, monotonicMillis() - cachedAtMillis)
            value.takeIf { ageMillis.milliseconds < maximumAge }
        }

    private suspend fun cachedValue(): GeolocationPositionResult.Success? =
        cacheLock.withLock { cached }

    private fun PlatformLocation.toGeolocation() = GeolocationPositionResult.Success(
        timestamp = Instant.fromEpochMilliseconds(timestampMs),
        latitude = latitudeE7 / E7_SCALE,
        longitude = longitudeE7 / E7_SCALE,
        accuracy = accuracyM.toDouble(),
        altitude = null,
        heading = null,
        speed = null,
    )

    private fun locationError(status: Int): String = when (status) {
        PlatformProviderController.STATUS_CANCELLED -> "Location request cancelled"
        PlatformProviderController.STATUS_INVALID_ARGUMENT -> "Invalid location request"
        PlatformProviderController.STATUS_NOT_SUPPORTED -> "Location not supported"
        PlatformProviderController.STATUS_BUSY -> "Location provider busy"
        PlatformProviderController.STATUS_UNAVAILABLE -> "Location not available"
        PlatformProviderController.STATUS_IO_ERROR -> "Location provider I/O error"
        PlatformProviderController.STATUS_PROTOCOL_ERROR -> "Invalid location provider response"
        else -> "Location provider error"
    }

    private companion object {
        const val MIN_WATCH_INTERVAL_MS = 200L
        const val E7_SCALE = 10_000_000.0
    }
}
