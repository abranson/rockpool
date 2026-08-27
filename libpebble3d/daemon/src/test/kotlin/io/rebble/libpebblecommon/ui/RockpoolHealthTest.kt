/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.database.entity.HRMonitoringInterval
import io.rebble.libpebblecommon.database.entity.HealthGender
import io.rebble.libpebblecommon.health.HealthSettings
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.delay
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class RockpoolHealthTest {
    @Test
    fun `legacy QML health values normalize to libpebble keys`() {
        assertEquals(
            mapOf(
                "health.enabled" to "true",
                "health.age" to "41",
                "health.gender" to "0",
                "health.height" to "183",
                "health.weight" to "72",
                "health.moreActive" to "true",
                "health.sleepMore" to "false",
            ),
            normalizeRockpoolHealthParams(
                linkedMapOf(
                    "enabled" to Variant(true),
                    "age" to Variant("41"),
                    "gender" to Variant("female"),
                    "height" to Variant("183"),
                    "weight" to Variant("72"),
                    "moreActive" to Variant(true),
                    "sleepMore" to Variant(false),
                ),
            ),
        )
    }

    @Test
    fun `legacy health normalization accepts bounded typed integers`() {
        assertEquals(
            mapOf("health.age" to "130", "health.gender" to "1"),
            normalizeRockpoolHealthParams(
                mapOf(
                    "age" to Variant(UInt32(130)),
                    "gender" to Variant(1),
                ),
            ),
        )
    }

    @Test
    fun `legacy health normalization rejects unknown malformed and unrepresentable values`() {
        listOf(
            emptyMap(),
            mapOf("unknown" to Variant(true)),
            mapOf("enabled" to Variant("true")),
            mapOf("gender" to Variant("invalid")),
            mapOf("gender" to Variant("other")),
            mapOf("gender" to Variant(2)),
            mapOf("age" to Variant("0")),
            mapOf("weight" to Variant("328")),
        ).forEach { values ->
            assertFailsWith<IllegalArgumentException> {
                normalizeRockpoolHealthParams(values)
            }
        }
    }

    @Test
    fun `legacy health record preserves units and gender strings`() {
        val record = rockpoolHealthParams(
            HealthSettings(
                heightMm = 1830,
                weightDag = 7200,
                trackingEnabled = true,
                activityInsightsEnabled = true,
                sleepInsightsEnabled = false,
                ageYears = 41,
                gender = HealthGender.Female,
                imperialUnits = false,
                hrmEnabled = true,
                hrmMeasurementInterval = HRMonitoringInterval.ThirtyMin,
                hrmActivityTrackingEnabled = true,
                restingHr = 60,
                elevatedHr = 120,
                maxHr = 180,
                hrZone1Threshold = 90,
                hrZone2Threshold = 110,
                hrZone3Threshold = 140,
            ),
        )

        assertEquals(true, record.getValue("enabled").value)
        assertEquals(41, record.getValue("age").value)
        assertEquals("female", record.getValue("gender").value)
        assertEquals(183, record.getValue("height").value)
        assertEquals(72, record.getValue("weight").value)
        assertEquals(true, record.getValue("moreActive").value)
        assertEquals(false, record.getValue("sleepMore").value)
    }

    @Test
    fun `health coordinator serializes concurrent read merge updates`() {
        val state = MutableStateFlow(healthSettings())
        val libPebble = object : LibPebble by FakeLibPebble() {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val coordinator = RockpoolHealthCoordinator(libPebble) {
            delay(25)
            state.value = it
        }
        val start = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        val age = thread(name = "rockpool-health-test-age") {
            runCatching {
                start.await()
                coordinator.update({ it.copy(ageYears = 52) }, { true })
            }.onFailure { failure.compareAndSet(null, it) }
        }
        val units = thread(name = "rockpool-health-test-units") {
            runCatching {
                start.await()
                coordinator.update({ it.copy(imperialUnits = true) }, { true })
            }.onFailure { failure.compareAndSet(null, it) }
        }

        start.countDown()
        age.join()
        units.join()
        failure.get()?.let { throw it }

        assertEquals(52, state.value.ageYears)
        assertEquals(true, state.value.imperialUnits)
    }

    @Test
    fun `health coordinator returns the serialized enable transition`() {
        val disabled = healthSettings().copy(trackingEnabled = false)
        val state = MutableStateFlow(disabled)
        val libPebble = object : LibPebble by FakeLibPebble() {
            override val healthSettings: Flow<HealthSettings> = state
        }

        val result = RockpoolHealthCoordinator(libPebble) { state.value = it }.updateWithResult(
            transform = { it.copy(trackingEnabled = true) },
            commit = { true },
        )

        assertEquals(true, result.saved)
        assertEquals(false, result.previous.trackingEnabled)
        assertEquals(true, result.current.trackingEnabled)
    }

    @Test
    fun `health sync throttle accepts only one request per minute`() {
        var nowNanos = 0L
        val throttle = RockpoolHealthSyncThrottle { nowNanos }

        assertEquals(true, throttle.tryAcquire())
        nowNanos += 59_999_999_999L
        assertEquals(false, throttle.tryAcquire())
        nowNanos += 1L
        assertEquals(true, throttle.tryAcquire())
    }

    private fun healthSettings() = HealthSettings(
        heightMm = 1830,
        weightDag = 7200,
        trackingEnabled = true,
        activityInsightsEnabled = true,
        sleepInsightsEnabled = false,
        ageYears = 41,
        gender = HealthGender.Female,
        imperialUnits = false,
        hrmEnabled = true,
        hrmMeasurementInterval = HRMonitoringInterval.ThirtyMin,
        hrmActivityTrackingEnabled = true,
        restingHr = 60,
        elevatedHr = 120,
        maxHr = 180,
        hrZone1Threshold = 90,
        hrZone2Threshold = 110,
        hrZone3Threshold = 140,
    )
}
