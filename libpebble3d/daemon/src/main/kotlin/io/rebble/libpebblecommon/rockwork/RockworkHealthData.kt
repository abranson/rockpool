/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.connection.HealthDataApi
import io.rebble.libpebblecommon.database.entity.HealthDataEntity
import io.rebble.libpebblecommon.database.entity.OverlayDataEntity
import io.rebble.libpebblecommon.health.OverlayType
import io.rebble.libpebblecommon.health.calculateSleepSearchWindow
import kotlinx.datetime.DatePeriod
import kotlinx.datetime.LocalDate
import kotlinx.datetime.LocalDateTime
import kotlinx.datetime.LocalTime
import kotlinx.datetime.TimeZone
import kotlinx.datetime.atStartOfDayIn
import kotlinx.datetime.minus
import kotlinx.datetime.plus
import kotlinx.datetime.toInstant
import kotlinx.datetime.toLocalDateTime
import org.freedesktop.dbus.types.Variant
import java.time.format.TextStyle
import java.util.Locale
import kotlin.time.Clock
import kotlin.time.Instant

internal const val ROCKWORK_HEALTH_MAX_DETAIL_SECONDS = 48L * 60L * 60L
internal const val ROCKWORK_HEALTH_MAX_AVERAGE_DAYS = 31

private const val OVERVIEW_DAYS = 30
private const val MAX_DETAIL_MOVEMENT_ROWS = 2 * 24 * 60
private const val MAX_DETAIL_OVERLAY_ROWS = 4096
private const val MAX_OVERVIEW_MOVEMENT_ROWS = (OVERVIEW_DAYS + 1) * 25 * 60
private const val MAX_OVERVIEW_OVERLAY_ROWS = 8192
private const val DAY_MILLIS = 24L * 60L * 60L * 1000L
private const val SLEEP_SESSION_GAP_SECONDS = 60L * 60L

/**
 * Bounded, read-only projection of libpebble3's account-global health database into the records
 * historically returned by org.rockwork.Pebble. This is deliberately independent of D-Bus
 * objects so the data contract can be tested before the compatibility interface is expanded.
 */
internal class RockworkHealthDataCoordinator(
    private val health: HealthDataApi,
    private val now: () -> Instant = { Clock.System.now() },
    private val timeZone: () -> TimeZone = { TimeZone.currentSystemDefault() },
    private val labelForDay: (LocalDate) -> String = ::localizedDayLabel,
) {
    suspend fun stepsDataForRange(
        startEpochSeconds: Long,
        endEpochSeconds: Long,
    ): Map<String, Variant<*>> {
        validateEpochRange(startEpochSeconds, endEpochSeconds)
        val rows = movementRows(
            start = startEpochSeconds,
            end = endEpochSeconds,
            limit = MAX_DETAIL_MOVEMENT_ROWS,
        )
        return movementRecord(movementSummary(rows), includeRows = true)
    }

    suspend fun stepsDataForWeek(startDate: LocalDate): Map<String, Variant<*>> {
        val zone = timeZone()
        val today = now().toLocalDateTime(zone).date
        val entries = mutableListOf<Variant<*>>()
        var maxSteps = 0
        var totalSteps = 0L
        var totalDistance = 0L
        var totalCalories = 0L
        var totalActiveMinutes = 0L
        var totalHeartRate = 0L
        var daysWithData = 0

        repeat(7) { offset ->
            val date = startDate.plus(DatePeriod(days = offset))
            if (date > today) {
                entries += recordVariant(
                    linkedMapOf(
                        "steps" to Variant(-1),
                        "averageHeartRate" to Variant(0),
                        "maxHeartRate" to Variant(0),
                    ),
                )
            } else {
                val summary = movementSummary(movementRowsForDate(date, zone))
                entries += recordVariant(
                    linkedMapOf(
                        "steps" to Variant(summary.totalSteps),
                        "averageHeartRate" to Variant(summary.averageHeartRate),
                        "maxHeartRate" to Variant(summary.maxHeartRate),
                    ),
                )
                if (summary.hasData) {
                    maxSteps = maxOf(maxSteps, summary.totalSteps)
                    totalSteps += summary.totalSteps
                    totalDistance += summary.distanceCm
                    totalCalories += summary.restingCalories + summary.activeCalories
                    totalActiveMinutes += summary.activeMinutes
                    totalHeartRate += summary.averageHeartRate
                    daysWithData++
                }
            }
        }

        return linkedMapOf(
            "steps" to Variant(entries, "av"),
            "maxSteps" to Variant(maxSteps),
            "averageSteps" to Variant(averageInt(totalSteps, daysWithData)),
            "averageDistance" to Variant(averageLong(totalDistance, daysWithData)),
            "averageCalories" to Variant(averageLong(totalCalories, daysWithData)),
            "averageActiveTime" to Variant(averageInt(totalActiveMinutes, daysWithData)),
            "averageHeartRate" to Variant(averageInt(totalHeartRate, daysWithData)),
        )
    }

    suspend fun averageStepsData(
        startDate: LocalDate,
        endDate: LocalDate,
    ): Map<String, Variant<*>> {
        val dayCount = boundedDayCount(startDate, endDate) ?: return emptyAverageStepsRecord()
        val zone = timeZone()
        val start = startDate.atStartOfDayIn(zone).epochSeconds
        val end = endDate.atStartOfDayIn(zone).epochSeconds
        val rows = movementRows(
            start = start,
            end = end,
            limit = dayCount * 25 * 60,
        )
        val summaries = rows.groupBy { row ->
            Instant.fromEpochSeconds(row.timestamp).toLocalDateTime(zone).date
        }.values.map(::movementSummary)

        val daysWithData = summaries.size
        return linkedMapOf(
            "averageSteps" to Variant(averageInt(summaries.sumOf { it.totalSteps.toLong() }, daysWithData)),
            "averageActiveTime" to Variant(averageInt(summaries.sumOf { it.activeMinutes.toLong() }, daysWithData)),
            "averageCalories" to Variant(
                averageLong(summaries.sumOf { it.restingCalories + it.activeCalories }, daysWithData),
            ),
            "averageDistance" to Variant(averageLong(summaries.sumOf { it.distanceCm }, daysWithData)),
            "averageHeartRate" to Variant(
                averageInt(summaries.sumOf { it.averageHeartRate.toLong() }, daysWithData),
            ),
            "daysWithData" to Variant(daysWithData),
        )
    }

    suspend fun overlayDataForRange(
        startEpochSeconds: Long,
        endEpochSeconds: Long,
        type: Int,
    ): List<Variant<*>> {
        validateEpochRange(startEpochSeconds, endEpochSeconds)
        require(OverlayType.fromValue(type) != null) { "Unknown health overlay type: $type" }
        val source = when (type) {
            OverlayType.Sleep.value,
            OverlayType.DeepSleep.value,
            OverlayType.Nap.value,
            OverlayType.DeepNap.value -> health.getSleepEntries(startEpochSeconds, endEpochSeconds)
            else -> health.getActivitySessions(startEpochSeconds, endEpochSeconds)
        }
        val rows = boundedOverlayRows(source, startEpochSeconds, endEpochSeconds)
            .filter { it.type == type }
        return rows.map(::overlayRecord)
    }

    suspend fun sleepDataForRange(
        startEpochSeconds: Long,
        endEpochSeconds: Long,
    ): List<Variant<*>> {
        validateEpochRange(startEpochSeconds, endEpochSeconds)
        return boundedOverlayRows(
            health.getSleepEntries(startEpochSeconds, endEpochSeconds),
            startEpochSeconds,
            endEpochSeconds,
        ).filter(::isLegacySleepEntry).map { entry ->
            recordVariant(
                linkedMapOf(
                    "starttime" to Variant(entry.startTime),
                    "duration" to Variant(legacyInt(entry.duration)),
                    "type" to Variant(entry.type),
                ),
            )
        }
    }

    suspend fun sleepDataForWeek(startDate: LocalDate): Map<String, Variant<*>> {
        val zone = timeZone()
        val today = now().toLocalDateTime(zone).date
        val lastDate = startDate.plus(DatePeriod(days = 6))
        val entries = sleepEntriesForDays(startDate, lastDate, zone)
        val records = mutableListOf<Variant<*>>()
        var totalDuration = 0L
        var maxDuration = 0L
        var daysWithData = 0

        repeat(7) { offset ->
            val date = startDate.plus(DatePeriod(days = offset))
            if (date > today) {
                records += sleepWeekVariant(-1, -1)
            } else {
                val session = legacySleepSession(entries, date.atStartOfDayIn(zone).epochSeconds)
                val totalSleep = session?.totalSleep ?: 0L
                val deepSleep = session?.deepSleep ?: 0L
                records += sleepWeekVariant(legacyInt(totalSleep), legacyInt(deepSleep))
                if (session != null) {
                    totalDuration += totalSleep
                    maxDuration = maxOf(maxDuration, totalSleep)
                    daysWithData++
                }
            }
        }

        return linkedMapOf(
            "sleep" to Variant(records, "av"),
            "averageDuration" to Variant(averageInt(totalDuration, daysWithData)),
            "maxDuration" to Variant(legacyInt(maxDuration)),
        )
    }

    suspend fun sleepAverage(
        startDate: LocalDate,
        endDate: LocalDate,
        type: Int,
    ): Int {
        val dayCount = boundedDayCount(startDate, endDate) ?: return 0
        require(type == OverlayType.Sleep.value || type == OverlayType.DeepSleep.value) {
            "Sleep average supports only normal or deep sleep"
        }
        val zone = timeZone()
        val entries = sleepEntriesForDays(
            firstDate = startDate,
            lastDate = endDate.minus(DatePeriod(days = 1)),
            zone = zone,
        )
        var total = 0L
        var count = 0
        repeat(dayCount) { offset ->
            val date = startDate.plus(DatePeriod(days = offset))
            val session = legacySleepSession(entries, date.atStartOfDayIn(zone).epochSeconds)
                ?: return@repeat
            total += if (type == OverlayType.DeepSleep.value) session.deepSleep else session.totalSleep
            count++
        }
        return averageInt(total, count)
    }

    suspend fun averageSleepTimes(day: LocalDate): Map<String, Variant<*>> {
        val zone = timeZone()
        val firstDate = day.minus(DatePeriod(days = OVERVIEW_DAYS - 1))
        val entries = sleepEntriesForDays(firstDate, day, zone)
        val weekend = day.dayOfWeek.ordinal >= 5
        var count = 0
        var fallAsleepMillis = 0L
        var wakeupMillis = 0L
        var sleepSeconds = 0L
        var deepSleepSeconds = 0L

        repeat(OVERVIEW_DAYS) { offset ->
            val target = day.minus(DatePeriod(days = offset))
            val targetWeekend = target.dayOfWeek.ordinal >= 5
            if (targetWeekend != weekend) return@repeat
            val session = legacySleepSession(entries, target.atStartOfDayIn(zone).epochSeconds)
                ?: return@repeat
            val fallAsleep = Instant.fromEpochSeconds(session.start).toLocalDateTime(zone).time
            val wakeup = Instant.fromEpochSeconds(session.end).toLocalDateTime(zone).time
            var fallMillis = millisSinceStartOfDay(fallAsleep)
            if (fallAsleep.hour < 12) fallMillis += DAY_MILLIS
            var wakeMillis = millisSinceStartOfDay(wakeup)
            if (wakeup.hour < 18) wakeMillis += DAY_MILLIS
            fallAsleepMillis += fallMillis
            wakeupMillis += wakeMillis
            sleepSeconds += session.totalSleep
            deepSleepSeconds += session.deepSleep
            count++
        }
        if (count == 0) return emptyMap()

        var sleepDate = day
        var averageFallMillis = fallAsleepMillis / count
        if (averageFallMillis > DAY_MILLIS) {
            averageFallMillis -= DAY_MILLIS
            sleepDate = sleepDate.minus(DatePeriod(days = 1))
        }
        var wakeDate = day
        var averageWakeMillis = wakeupMillis / count
        if (averageWakeMillis > DAY_MILLIS) {
            averageWakeMillis -= DAY_MILLIS
            wakeDate = wakeDate.minus(DatePeriod(days = 1))
        }

        return linkedMapOf(
            "fallasleep" to Variant(epochSecondsAtMillis(sleepDate, averageFallMillis, zone)),
            "wakeup" to Variant(epochSecondsAtMillis(wakeDate, averageWakeMillis, zone)),
            "sleepTime" to Variant(sleepSeconds / count),
            "deepSleep" to Variant(deepSleepSeconds / count),
        )
    }

    suspend fun healthOverview(): Map<String, Variant<*>> {
        val zone = timeZone()
        val today = now().toLocalDateTime(zone).date
        val averageStart = today.minus(DatePeriod(days = OVERVIEW_DAYS))
        val tomorrow = today.plus(DatePeriod(days = 1))
        val movementStart = averageStart.atStartOfDayIn(zone).epochSeconds
        val movementEnd = tomorrow.atStartOfDayIn(zone).epochSeconds
        val movement = movementRows(
            start = movementStart,
            end = movementEnd,
            limit = MAX_OVERVIEW_MOVEMENT_ROWS,
        )
        val movementByDate = movement.groupBy { row ->
            Instant.fromEpochSeconds(row.timestamp).toLocalDateTime(zone).date
        }.mapValues { (_, rows) -> movementSummary(rows) }
        val sleep = sleepEntriesForDays(averageStart, today, zone)
        val sleepByDate = buildMap {
            repeat(OVERVIEW_DAYS + 1) { offset ->
                val date = averageStart.plus(DatePeriod(days = offset))
                legacySleepSession(sleep, date.atStartOfDayIn(zone).epochSeconds)?.let {
                    put(date, it)
                }
            }
        }

        val todayMovement = movementByDate[today] ?: MovementSummary.EMPTY
        val averageMovement = (0 until OVERVIEW_DAYS).mapNotNull { offset ->
            movementByDate[averageStart.plus(DatePeriod(days = offset))]
        }
        val averageSleep = (0 until OVERVIEW_DAYS).mapNotNull { offset ->
            sleepByDate[averageStart.plus(DatePeriod(days = offset))]
        }
        val lastNight = sleepByDate[today]

        val stepsWeek = mutableListOf<Variant<*>>()
        val sleepWeek = mutableListOf<Variant<*>>()
        for (offset in 6 downTo 0) {
            val date = today.minus(DatePeriod(days = offset))
            val movementSummary = movementByDate[date] ?: MovementSummary.EMPTY
            stepsWeek += recordVariant(
                linkedMapOf(
                    "label" to Variant(labelForDay(date)),
                    "date" to Variant(date.toString()),
                    "steps" to Variant(movementSummary.totalSteps),
                    "averageHeartRate" to Variant(movementSummary.averageHeartRate),
                    "maxHeartRate" to Variant(movementSummary.maxHeartRate),
                ),
            )
            val sleepSummary = sleepByDate[date]
            sleepWeek += recordVariant(
                linkedMapOf(
                    "label" to Variant(labelForDay(date)),
                    "date" to Variant(date.toString()),
                    "sleepDuration" to Variant(legacyInt(sleepSummary?.totalSleep ?: 0L)),
                    "deepSleepDuration" to Variant(legacyInt(sleepSummary?.deepSleep ?: 0L)),
                ),
            )
        }

        return linkedMapOf(
            "todaySteps" to Variant(todayMovement.totalSteps),
            "averageStepsPerDay" to Variant(
                averageInt(averageMovement.sumOf { it.totalSteps.toLong() }, averageMovement.size),
            ),
            "lastNightSleepSeconds" to Variant(legacyInt(lastNight?.totalSleep ?: 0L)),
            "lastNightDeepSleepSeconds" to Variant(legacyInt(lastNight?.deepSleep ?: 0L)),
            "averageSleepSecondsPerDay" to Variant(
                averageInt(averageSleep.sumOf { it.totalSleep }, averageSleep.size),
            ),
            "todayAverageHeartRate" to Variant(todayMovement.averageHeartRate),
            "todayMaxHeartRate" to Variant(todayMovement.maxHeartRate),
            "latestHeartRate" to Variant(todayMovement.latestHeartRate),
            "latestHeartRateTimestamp" to Variant(todayMovement.latestHeartRateTimestamp),
            "averageHeartRate30Days" to Variant(
                averageInt(averageMovement.sumOf { it.averageHeartRate.toLong() }, averageMovement.size),
            ),
            "latestDataTimestamp" to Variant(health.getLatestTimestamp() ?: 0L),
            "daysOfData" to Variant(maxOf(averageMovement.size, averageSleep.size)),
            "stepsWeek" to Variant(stepsWeek, "av"),
            "sleepWeek" to Variant(sleepWeek, "av"),
        )
    }

    private suspend fun movementRowsForDate(
        date: LocalDate,
        zone: TimeZone,
    ): List<HealthDataEntity> {
        val start = date.atStartOfDayIn(zone).epochSeconds
        val end = date.plus(DatePeriod(days = 1)).atStartOfDayIn(zone).epochSeconds
        return movementRows(start, end, MAX_DETAIL_MOVEMENT_ROWS)
    }

    private suspend fun movementRows(
        start: Long,
        end: Long,
        limit: Int,
    ): List<HealthDataEntity> {
        val rows = health.getHealthDataForRange(start, end)
            .filter { it.timestamp >= start && it.timestamp < end }
            .sortedBy { it.timestamp }
        require(rows.size <= limit) { "Health movement result exceeds $limit records" }
        return rows
    }

    private suspend fun sleepEntriesForDays(
        firstDate: LocalDate,
        lastDate: LocalDate,
        zone: TimeZone,
    ): List<OverlayDataEntity> {
        require(lastDate >= firstDate) { "Invalid sleep date range" }
        val firstWindow = calculateSleepSearchWindow(firstDate.atStartOfDayIn(zone).epochSeconds)
        val lastWindow = calculateSleepSearchWindow(lastDate.atStartOfDayIn(zone).epochSeconds)
        val rows = health.getSleepEntries(firstWindow.first, lastWindow.second)
            .filter(::isLegacySleepEntry)
            .filter { it.startTime >= firstWindow.first && it.startTime < lastWindow.second }
            .sortedBy { it.startTime }
        require(rows.size <= MAX_OVERVIEW_OVERLAY_ROWS) {
            "Health sleep result exceeds $MAX_OVERVIEW_OVERLAY_ROWS records"
        }
        return rows
    }

    private fun boundedOverlayRows(
        source: List<OverlayDataEntity>,
        start: Long,
        end: Long,
    ): List<OverlayDataEntity> {
        val rows = source.filter { it.startTime >= start && it.startTime < end }
            .sortedBy { it.startTime }
        require(rows.size <= MAX_DETAIL_OVERLAY_ROWS) {
            "Health overlay result exceeds $MAX_DETAIL_OVERLAY_ROWS records"
        }
        return rows
    }

    private fun boundedDayCount(startDate: LocalDate, endDate: LocalDate): Int? {
        if (startDate >= endDate) return null
        var current = startDate
        var count = 0
        while (current < endDate && count <= ROCKWORK_HEALTH_MAX_AVERAGE_DAYS) {
            current = current.plus(DatePeriod(days = 1))
            count++
        }
        require(current == endDate && count <= ROCKWORK_HEALTH_MAX_AVERAGE_DAYS) {
            "Health date range exceeds $ROCKWORK_HEALTH_MAX_AVERAGE_DAYS days"
        }
        return count
    }

    private fun validateEpochRange(start: Long, end: Long) {
        val duration = end - start
        require(end > start && duration > 0) { "Health range must have an exclusive end after start" }
        require(duration <= ROCKWORK_HEALTH_MAX_DETAIL_SECONDS) {
            "Health range exceeds $ROCKWORK_HEALTH_MAX_DETAIL_SECONDS seconds"
        }
    }
}

private data class MovementSummary(
    val rows: List<HealthDataEntity>,
    val totalSteps: Int,
    val activeMinutes: Int,
    val restingCalories: Long,
    val activeCalories: Long,
    val distanceCm: Long,
    val averageHeartRate: Int,
    val maxHeartRate: Int,
    val latestHeartRate: Int,
    val latestHeartRateTimestamp: Long,
) {
    val hasData: Boolean get() = rows.isNotEmpty()

    companion object {
        val EMPTY = MovementSummary(emptyList(), 0, 0, 0, 0, 0, 0, 0, 0, 0)
    }
}

private data class LegacySleepSession(
    var start: Long,
    var end: Long,
    var totalSleep: Long,
    var deepSleep: Long,
)

private fun movementSummary(rows: List<HealthDataEntity>): MovementSummary {
    if (rows.isEmpty()) return MovementSummary.EMPTY
    val sorted = rows.sortedBy { it.timestamp }
    val heartRates = sorted.filter { it.heartRate > 0 }
    val latest = heartRates.lastOrNull()
    return MovementSummary(
        rows = sorted,
        totalSteps = legacyInt(sorted.sumOf { it.steps.toLong() }),
        activeMinutes = legacyInt(sorted.sumOf { it.activeMinutes.toLong() }),
        restingCalories = sorted.sumOf { it.restingGramCalories.toLong() },
        activeCalories = sorted.sumOf { it.activeGramCalories.toLong() },
        distanceCm = sorted.sumOf { it.distanceCm.toLong() },
        averageHeartRate = averageInt(heartRates.sumOf { it.heartRate.toLong() }, heartRates.size),
        maxHeartRate = heartRates.maxOfOrNull { it.heartRate } ?: 0,
        latestHeartRate = latest?.heartRate ?: 0,
        latestHeartRateTimestamp = latest?.timestamp ?: 0L,
    )
}

private fun movementRecord(
    summary: MovementSummary,
    includeRows: Boolean,
): Map<String, Variant<*>> = linkedMapOf<String, Variant<*>>().apply {
    if (includeRows) {
        put(
            "steps",
            Variant(
                summary.rows.map { row ->
                    recordVariant(
                        linkedMapOf(
                            "steps" to Variant(row.steps),
                            "timestamp" to Variant(row.timestamp),
                            "heart_rate" to Variant(row.heartRate),
                        ),
                    )
                },
                "av",
            ),
        )
    }
    put("totalSteps", Variant(summary.totalSteps))
    put("active_minutes", Variant(summary.activeMinutes))
    put("resting_calories", Variant(summary.restingCalories))
    put("active_calories", Variant(summary.activeCalories))
    put("distance_cm", Variant(legacyInt(summary.distanceCm)))
    put("averageHeartRate", Variant(summary.averageHeartRate))
    put("maxHeartRate", Variant(summary.maxHeartRate))
    put("latestHeartRate", Variant(summary.latestHeartRate))
    put("latestHeartRateTimestamp", Variant(summary.latestHeartRateTimestamp))
}

private fun overlayRecord(entry: OverlayDataEntity): Variant<*> = recordVariant(
    linkedMapOf(
        "starttime" to Variant(entry.startTime),
        "duration" to Variant(legacyInt(entry.duration)),
        "type" to Variant(entry.type),
        "steps" to Variant(entry.steps),
        "resting_kilo_calories" to Variant(entry.restingKiloCalories),
        "active_kilo_calories" to Variant(entry.activeKiloCalories),
        "distance_cm" to Variant(entry.distanceCm),
        "offset_utc" to Variant(entry.offsetUTC),
    ),
)

private fun sleepWeekVariant(total: Int, deep: Int): Variant<*> = recordVariant(
    linkedMapOf(
        "sleepDuration" to Variant(total),
        "deepSleepDuration" to Variant(deep),
    ),
)

private fun emptyAverageStepsRecord(): Map<String, Variant<*>> = linkedMapOf(
    "averageSteps" to Variant(0),
    "averageActiveTime" to Variant(0),
    "averageCalories" to Variant(0L),
    "averageDistance" to Variant(0L),
    "averageHeartRate" to Variant(0),
    "daysWithData" to Variant(0),
)

private fun legacySleepSession(
    entries: List<OverlayDataEntity>,
    dayStartEpochSeconds: Long,
): LegacySleepSession? {
    val (searchStart, searchEnd) = calculateSleepSearchWindow(dayStartEpochSeconds)
    val sessions = mutableListOf<LegacySleepSession>()
    entries.asSequence()
        .filter(::isLegacySleepEntry)
        .filter { it.startTime >= searchStart && it.startTime < searchEnd }
        .sortedBy { it.startTime }
        .forEach { entry ->
            val end = entry.startTime + entry.duration
            val current = sessions.lastOrNull()?.takeIf {
                entry.startTime <= it.end + SLEEP_SESSION_GAP_SECONDS
            }
            if (current != null) {
                current.end = maxOf(current.end, end)
                if (entry.type == OverlayType.Sleep.value) {
                    current.totalSleep += entry.duration
                    if (current.start == 0L || entry.startTime < current.start) {
                        current.start = entry.startTime
                    }
                } else {
                    current.deepSleep += entry.duration
                }
            } else {
                sessions += LegacySleepSession(
                    start = if (entry.type == OverlayType.Sleep.value) entry.startTime else 0L,
                    end = end,
                    totalSleep = if (entry.type == OverlayType.Sleep.value) entry.duration else 0L,
                    deepSleep = if (entry.type == OverlayType.DeepSleep.value) entry.duration else 0L,
                )
            }
        }
    val best = sessions.maxByOrNull { it.totalSleep }?.takeIf { it.totalSleep > 0 } ?: return null
    if (best.start == 0L) best.start = best.end - best.totalSleep
    return best
}

private fun isLegacySleepEntry(entry: OverlayDataEntity): Boolean =
    entry.type == OverlayType.Sleep.value || entry.type == OverlayType.DeepSleep.value

private fun recordVariant(values: Map<String, Variant<*>>): Variant<*> = Variant(values, "a{sv}")

private fun averageInt(total: Long, count: Int): Int =
    if (count == 0) 0 else legacyInt(total / count)

private fun averageLong(total: Long, count: Int): Long = if (count == 0) 0L else total / count

private fun legacyInt(value: Long): Int = value.coerceIn(Int.MIN_VALUE.toLong(), Int.MAX_VALUE.toLong()).toInt()

private fun millisSinceStartOfDay(time: LocalTime): Long =
    (((time.hour * 60L + time.minute) * 60L + time.second) * 1000L) + time.nanosecond / 1_000_000L

private fun epochSecondsAtMillis(date: LocalDate, millis: Long, zone: TimeZone): Long {
    val normalized = millis.coerceIn(0L, DAY_MILLIS - 1)
    val totalSeconds = normalized / 1000L
    val hour = (totalSeconds / 3600L).toInt()
    val minute = ((totalSeconds % 3600L) / 60L).toInt()
    val second = (totalSeconds % 60L).toInt()
    return LocalDateTime(date, LocalTime(hour, minute, second)).toInstant(zone).epochSeconds
}

private fun localizedDayLabel(date: LocalDate): String =
    java.time.DayOfWeek.of(date.dayOfWeek.ordinal + 1)
        .getDisplayName(TextStyle.SHORT, Locale.getDefault())
        .take(1)
        .ifEmpty { "?" }
