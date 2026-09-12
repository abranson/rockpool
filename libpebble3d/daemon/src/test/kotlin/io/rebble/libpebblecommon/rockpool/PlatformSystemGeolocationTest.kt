/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.util.GeolocationPositionResult
import io.rebble.libpebblecommon.util.SystemGeolocation
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

class PlatformSystemGeolocationTest {
    @Test
    fun freshDefaultAndExplicitCacheMaximumAge() = runBlocking {
        var now = 1_000L
        var queries = 0
        val geolocation = PlatformSystemGeolocation(
            queryLocation = { _, _ -> queries++; success() },
            monotonicMillis = { now },
        )

        geolocation.getCurrentPosition()
        now += 10
        geolocation.getCurrentPosition()
        assertEquals(2, queries)

        geolocation.getCurrentPosition(maximumAge = 1.seconds)
        assertEquals(2, queries)
        now += 2
        geolocation.getCurrentPosition(maximumAge = 1.milliseconds)
        assertEquals(3, queries)
    }

    @Test
    fun queryMapsAccuracyTimeoutAndNullableFields() = runBlocking {
        val requests = mutableListOf<Pair<Boolean, Long>>()
        val geolocation = PlatformSystemGeolocation(
            queryLocation = { fine, timeout ->
                requests += fine to timeout.inWholeMilliseconds
                success()
            },
        )

        val coarse = assertIs<GeolocationPositionResult.Success>(
            geolocation.getCurrentPosition(maximumAge = 0.milliseconds, timeout = 0.milliseconds),
        )
        val fine = assertIs<GeolocationPositionResult.Success>(
            geolocation.getCurrentPosition(
                maximumAge = 0.milliseconds,
                timeout = 90.seconds,
                highAccuracy = true,
            ),
        )

        assertEquals(listOf(false to 1L, true to 30_000L), requests)
        assertEquals(12.3456789, coarse.latitude)
        assertEquals(-98.7654321, coarse.longitude)
        assertEquals(42.0, coarse.accuracy)
        assertEquals(1_234L, coarse.timestamp.toEpochMilliseconds())
        assertNull(coarse.altitude)
        assertNull(coarse.heading)
        assertNull(coarse.speed)
        assertEquals(coarse, fine)
    }

    @Test
    fun staleProviderFailureFallsBackToCacheThenMapsError() = runBlocking {
        var now = 0L
        var succeed = true
        val geolocation = PlatformSystemGeolocation(
            queryLocation = { _, _ ->
                if (succeed) success() else PlatformLocationQueryResult.Error(
                    PlatformProviderController.STATUS_NOT_SUPPORTED,
                )
            },
            monotonicMillis = { now },
        )

        assertIs<GeolocationPositionResult.Success>(geolocation.getCurrentPosition())
        now += SystemGeolocation.DEFAULT_MAX_AGE.inWholeMilliseconds
        succeed = false
        assertIs<GeolocationPositionResult.Success>(geolocation.getCurrentPosition())

        val noCache = PlatformSystemGeolocation(
            queryLocation = {
                _, _ -> PlatformLocationQueryResult.Error(
                    PlatformProviderController.STATUS_NOT_SUPPORTED,
                )
            },
        )
        assertEquals(
            "Location not supported",
            assertIs<GeolocationPositionResult.Error>(noCache.getCurrentPosition()).message,
        )
    }

    @Test
    fun cancellationPropagatesToCaller() = runBlocking {
        val entered = CompletableDeferred<Unit>()
        val geolocation = PlatformSystemGeolocation(
            queryLocation = { _, _ ->
                entered.complete(Unit)
                CompletableDeferred<PlatformLocationQueryResult>().await()
            },
        )
        val request = async { geolocation.getCurrentPosition(maximumAge = 0.milliseconds) }

        entered.await()
        request.cancelAndJoin()
    }

    @Test
    fun watchQueriesImmediatelyAtClampedCadenceAndCancels() = runBlocking {
        var queries = 0
        val geolocation = PlatformSystemGeolocation(
            queryLocation = { _, _ -> queries++; success() },
        )
        val results = withTimeout(2.seconds) {
            geolocation.watchPosition(1.milliseconds).take(2).toList()
        }
        assertEquals(2, queries)
        assertEquals(2, results.size)

        val running = launch { geolocation.watchPosition(1.milliseconds).collect { } }
        withTimeout(1.seconds) { while (queries < 3) kotlinx.coroutines.delay(1) }
        running.cancelAndJoin()
    }

    private fun success() = PlatformLocationQueryResult.Success(
        PlatformLocation(123_456_789, -987_654_321, 42, 1_234),
    )
}
