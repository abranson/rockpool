/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.HealthDataApi
import io.rebble.libpebblecommon.database.entity.HealthDataEntity
import io.rebble.libpebblecommon.database.entity.OverlayDataEntity
import io.rebble.libpebblecommon.health.OverlayType
import kotlinx.coroutines.runBlocking
import kotlinx.datetime.DatePeriod
import kotlinx.datetime.LocalDate
import kotlinx.datetime.TimeZone
import kotlinx.datetime.atStartOfDayIn
import kotlinx.datetime.minus
import kotlinx.datetime.plus
import org.freedesktop.dbus.types.Variant
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue
import kotlin.time.Instant

class RockworkHealthDataTest {
    @Test
    fun `day detail preserves legacy fields and exclusive end`() = runBlocking {
        val date = LocalDate(2024, 2, 29)
        val start = date.atStartOfDayIn(TimeZone.UTC).epochSeconds
        val end = date.plus(DatePeriod(days = 1)).atStartOfDayIn(TimeZone.UTC).epochSeconds
        val fake = FakeHealthData(
            movement = listOf(
                movement(start, steps = 10, heartRate = 60, activeMinutes = 1,
                    restingCalories = 100, activeCalories = 20, distanceCm = 30),
                movement(start + 60, steps = 20, heartRate = 0, activeMinutes = 0,
                    restingCalories = 200, activeCalories = 30, distanceCm = 40),
                movement(end, steps = 999, heartRate = 200),
            ),
        )
        val coordinator = coordinator(fake, date)

        val actual = coordinator.stepsDataForRange(start, end)

        assertPlainEquals(
            mapOf(
                "steps" to listOf(
                    mapOf<String, Any>("steps" to 10, "timestamp" to start, "heart_rate" to 60),
                    mapOf<String, Any>("steps" to 20, "timestamp" to start + 60, "heart_rate" to 0),
                ),
                "totalSteps" to 30,
                "active_minutes" to 1,
                "resting_calories" to 300L,
                "active_calories" to 50L,
                "distance_cm" to 70,
                "averageHeartRate" to 60,
                "maxHeartRate" to 60,
                "latestHeartRate" to 60,
                "latestHeartRateTimestamp" to start,
            ),
            unwrap(actual),
        )
        assertEquals("av", actual.getValue("steps").sig)
        assertTrue(variantRecords(actual.getValue("steps")).all { it.sig == "a{sv}" })
        assertEquals(listOf(start to end), fake.movementCalls)
    }

    @Test
    fun `daily ranges follow local midnight across DST`() = runBlocking {
        val startDate = LocalDate(2024, 3, 31)
        val zone = TimeZone.of("Europe/Paris")
        val fake = FakeHealthData()
        val coordinator = RockworkHealthDataCoordinator(
            health = fake,
            now = { Instant.parse("2024-04-10T12:00:00Z") },
            timeZone = { zone },
            labelForDay = { it.toString() },
        )

        coordinator.stepsDataForWeek(startDate)

        val first = fake.movementCalls.first()
        assertEquals(startDate.atStartOfDayIn(zone).epochSeconds, first.first)
        assertEquals(
            startDate.plus(DatePeriod(days = 1)).atStartOfDayIn(zone).epochSeconds,
            first.second,
        )
        assertEquals(23L * 60L * 60L, first.second - first.first)
    }

    @Test
    fun `overview batches thirty days and matches legacy dashboard fields`() = runBlocking {
        val today = LocalDate(2024, 3, 1)
        val zone = TimeZone.UTC
        val feb28 = LocalDate(2024, 2, 28)
        val feb29 = LocalDate(2024, 2, 29)
        val todayStart = today.atStartOfDayIn(zone).epochSeconds
        val feb28Start = feb28.atStartOfDayIn(zone).epochSeconds
        val feb29Start = feb29.atStartOfDayIn(zone).epochSeconds
        val movement = listOf(
            movement(feb28Start, steps = 100, heartRate = 50),
            movement(feb29Start, steps = 80, heartRate = 60),
            movement(feb29Start + 60, steps = 120, heartRate = 80),
            movement(todayStart, steps = 10, heartRate = 60),
            movement(todayStart + 60, steps = 20, heartRate = 90),
        )
        val overlays = buildList {
            addSleepDay(feb28, zone, totalHours = 7, deepHours = 1)
            addSleepDay(feb29, zone, totalHours = 6, deepHours = 2)
            addSleepDay(today, zone, totalHours = 8, deepHours = 3)
            add(
                overlay(
                    start = today.minus(DatePeriod(days = 1)).atStartOfDayIn(zone).epochSeconds +
                        20 * 60 * 60,
                    duration = 10 * 60 * 60,
                    type = OverlayType.Nap.value,
                ),
            )
        }
        val fake = FakeHealthData(
            movement = movement,
            overlays = overlays,
            latestTimestamp = todayStart + 60,
        )
        val coordinator = RockworkHealthDataCoordinator(
            health = fake,
            now = { Instant.parse("2024-03-01T12:00:00Z") },
            timeZone = { zone },
            labelForDay = { "L${it.day}" },
        )

        val actual = coordinator.healthOverview()
        val plain = unwrap(actual) as Map<*, *>

        assertEquals(30, plain["todaySteps"])
        assertEquals(150, plain["averageStepsPerDay"])
        assertEquals(8 * 60 * 60, plain["lastNightSleepSeconds"])
        assertEquals(3 * 60 * 60, plain["lastNightDeepSleepSeconds"])
        assertEquals(6 * 60 * 60 + 30 * 60, plain["averageSleepSecondsPerDay"])
        assertEquals(75, plain["todayAverageHeartRate"])
        assertEquals(90, plain["todayMaxHeartRate"])
        assertEquals(90, plain["latestHeartRate"])
        assertEquals(todayStart + 60, plain["latestHeartRateTimestamp"])
        assertEquals(60, plain["averageHeartRate30Days"])
        assertEquals(todayStart + 60, plain["latestDataTimestamp"])
        assertEquals(2, plain["daysOfData"])

        val stepsWeek = plain["stepsWeek"] as List<*>
        val sleepWeek = plain["sleepWeek"] as List<*>
        assertEquals(7, stepsWeek.size)
        assertEquals(7, sleepWeek.size)
        assertEquals(
            mapOf(
                "label" to "L1",
                "date" to "2024-03-01",
                "steps" to 30,
                "averageHeartRate" to 75,
                "maxHeartRate" to 90,
            ),
            stepsWeek.last(),
        )
        assertEquals(
            mapOf(
                "label" to "L1",
                "date" to "2024-03-01",
                "sleepDuration" to 8 * 60 * 60,
                "deepSleepDuration" to 3 * 60 * 60,
            ),
            sleepWeek.last(),
        )
        assertEquals("av", actual.getValue("stepsWeek").sig)
        assertEquals("av", actual.getValue("sleepWeek").sig)
        assertEquals(1, fake.movementCalls.size)
        assertEquals(1, fake.sleepCalls.size)
    }

    @Test
    fun `sleep and overlay methods preserve legacy session and record semantics`() = runBlocking {
        val date = LocalDate(2024, 3, 1)
        val zone = TimeZone.UTC
        val dayStart = date.atStartOfDayIn(zone).epochSeconds
        val sleepStart = date.minus(DatePeriod(days = 1)).atStartOfDayIn(zone).epochSeconds +
            22 * 60 * 60
        val walkStart = dayStart + 9 * 60 * 60
        val fake = FakeHealthData(
            overlays = listOf(
                overlay(sleepStart, 8 * 60 * 60, OverlayType.Sleep.value),
                overlay(sleepStart + 60 * 60, 2 * 60 * 60, OverlayType.DeepSleep.value),
                overlay(sleepStart + 30 * 60, 20 * 60, OverlayType.Nap.value),
                overlay(
                    walkStart,
                    30 * 60,
                    OverlayType.Walk.value,
                    steps = 3000,
                    restingCalories = 10,
                    activeCalories = 150,
                    distanceCm = 2200,
                    offsetUtc = 3600,
                ),
            ),
        )
        val coordinator = coordinator(fake, date)

        assertEquals(
            listOf(
                mapOf(
                    "starttime" to sleepStart,
                    "duration" to 8 * 60 * 60,
                    "type" to OverlayType.Sleep.value,
                ),
                mapOf(
                    "starttime" to sleepStart + 60 * 60,
                    "duration" to 2 * 60 * 60,
                    "type" to OverlayType.DeepSleep.value,
                ),
            ),
            unwrap(coordinator.sleepDataForRange(sleepStart, dayStart + 14 * 60 * 60)),
        )
        assertEquals(
            listOf(
                mapOf(
                    "starttime" to walkStart,
                    "duration" to 30 * 60,
                    "type" to OverlayType.Walk.value,
                    "steps" to 3000,
                    "resting_kilo_calories" to 10,
                    "active_kilo_calories" to 150,
                    "distance_cm" to 2200,
                    "offset_utc" to 3600,
                ),
            ),
            unwrap(
                coordinator.overlayDataForRange(
                    dayStart,
                    dayStart + ROCKWORK_HEALTH_MAX_DETAIL_SECONDS,
                    OverlayType.Walk.value,
                ),
            ),
        )
        assertEquals(8 * 60 * 60, coordinator.sleepAverage(
            date,
            date.plus(DatePeriod(days = 1)),
            OverlayType.Sleep.value,
        ))
        assertEquals(2 * 60 * 60, coordinator.sleepAverage(
            date,
            date.plus(DatePeriod(days = 1)),
            OverlayType.DeepSleep.value,
        ))
        val week = unwrap(coordinator.sleepDataForWeek(date)) as Map<*, *>
        assertEquals(
            mapOf("sleepDuration" to 8 * 60 * 60, "deepSleepDuration" to 2 * 60 * 60),
            (week["sleep"] as List<*>).first(),
        )
    }

    @Test
    fun `invalid and oversized reads fail before querying health storage`() = runBlocking {
        val date = LocalDate(2024, 3, 1)
        val fake = FakeHealthData()
        val coordinator = coordinator(fake, date)

        assertFailsWith<IllegalArgumentException> { coordinator.stepsDataForRange(10, 10) }
        assertFailsWith<IllegalArgumentException> {
            coordinator.stepsDataForRange(0, ROCKWORK_HEALTH_MAX_DETAIL_SECONDS + 1)
        }
        assertFailsWith<IllegalArgumentException> {
            coordinator.stepsDataForRange(Long.MIN_VALUE, Long.MAX_VALUE)
        }
        assertFailsWith<IllegalArgumentException> {
            coordinator.averageStepsData(date, date.plus(DatePeriod(days = 32)))
        }
        assertFailsWith<IllegalArgumentException> {
            coordinator.sleepAverage(
                date,
                date.plus(DatePeriod(days = 1)),
                OverlayType.Nap.value,
            )
        }

        assertTrue(fake.movementCalls.isEmpty())
        assertTrue(fake.sleepCalls.isEmpty())
        assertTrue(fake.activityCalls.isEmpty())
    }

    private fun coordinator(fake: HealthDataApi, date: LocalDate) = RockworkHealthDataCoordinator(
        health = fake,
        now = { date.atStartOfDayIn(TimeZone.UTC) },
        timeZone = { TimeZone.UTC },
        labelForDay = { it.toString() },
    )
}

private class FakeHealthData(
    private val movement: List<HealthDataEntity> = emptyList(),
    private val overlays: List<OverlayDataEntity> = emptyList(),
    private val latestTimestamp: Long? = movement.maxOfOrNull { it.timestamp },
) : HealthDataApi by FakeLibPebble() {
    val movementCalls = mutableListOf<Pair<Long, Long>>()
    val sleepCalls = mutableListOf<Pair<Long, Long>>()
    val activityCalls = mutableListOf<Pair<Long, Long>>()

    override suspend fun getLatestTimestamp(): Long? = latestTimestamp

    override suspend fun getHealthDataForRange(start: Long, end: Long): List<HealthDataEntity> {
        movementCalls += start to end
        return movement.filter { it.timestamp >= start && it.timestamp < end }
    }

    override suspend fun getSleepEntries(start: Long, end: Long): List<OverlayDataEntity> {
        sleepCalls += start to end
        return overlays.filter {
            it.type in OverlayType.Sleep.value..OverlayType.DeepNap.value &&
                it.startTime >= start && it.startTime < end
        }
    }

    override suspend fun getActivitySessions(start: Long, end: Long): List<OverlayDataEntity> {
        activityCalls += start to end
        return overlays.filter {
            it.type in OverlayType.Walk.value..OverlayType.OpenWorkout.value &&
                it.startTime >= start && it.startTime < end
        }
    }
}

private fun movement(
    timestamp: Long,
    steps: Int,
    heartRate: Int,
    activeMinutes: Int = 0,
    restingCalories: Int = 0,
    activeCalories: Int = 0,
    distanceCm: Int = 0,
): HealthDataEntity = HealthDataEntity(
    timestamp = timestamp,
    steps = steps,
    orientation = 0,
    intensity = 0,
    lightIntensity = 0,
    activeMinutes = activeMinutes,
    restingGramCalories = restingCalories,
    activeGramCalories = activeCalories,
    distanceCm = distanceCm,
    heartRate = heartRate,
)

private fun overlay(
    start: Long,
    duration: Int,
    type: Int,
    steps: Int = 0,
    restingCalories: Int = 0,
    activeCalories: Int = 0,
    distanceCm: Int = 0,
    offsetUtc: Int = 0,
): OverlayDataEntity = OverlayDataEntity(
    startTime = start,
    duration = duration.toLong(),
    type = type,
    steps = steps,
    restingKiloCalories = restingCalories,
    activeKiloCalories = activeCalories,
    distanceCm = distanceCm,
    offsetUTC = offsetUtc,
)

private fun MutableList<OverlayDataEntity>.addSleepDay(
    date: LocalDate,
    zone: TimeZone,
    totalHours: Int,
    deepHours: Int,
) {
    val start = date.minus(DatePeriod(days = 1)).atStartOfDayIn(zone).epochSeconds + 22 * 60 * 60
    add(overlay(start, totalHours * 60 * 60, OverlayType.Sleep.value))
    add(overlay(start + 60 * 60, deepHours * 60 * 60, OverlayType.DeepSleep.value))
}

@Suppress("UNCHECKED_CAST")
private fun variantRecords(value: Variant<*>): List<Variant<*>> = value.value as List<Variant<*>>

private fun unwrap(value: Any?): Any? = when (value) {
    is Variant<*> -> unwrap(value.value)
    is Map<*, *> -> value.entries.associate { (key, entryValue) -> key to unwrap(entryValue) }
    is List<*> -> value.map(::unwrap)
    else -> value
}

private fun assertPlainEquals(expected: Any?, actual: Any?, path: String = "root") {
    when (expected) {
        is Map<*, *> -> {
            require(actual is Map<*, *>) { "$path is ${actual?.javaClass}, expected Map" }
            assertEquals(expected.keys, actual.keys, "$path keys")
            expected.forEach { (key, value) ->
                assertPlainEquals(value, actual[key], "$path.$key")
            }
        }
        is List<*> -> {
            require(actual is List<*>) { "$path is ${actual?.javaClass}, expected List" }
            assertEquals(expected.size, actual.size, "$path size")
            expected.indices.forEach { index ->
                assertPlainEquals(expected[index], actual[index], "$path[$index]")
            }
        }
        else -> {
            assertEquals(expected?.javaClass, actual?.javaClass, "$path type")
            assertEquals(expected, actual, path)
        }
    }
}
