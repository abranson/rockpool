/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.calendar.CalendarEvent
import io.rebble.libpebblecommon.calendar.EventAttendee
import io.rebble.libpebblecommon.calendar.EventReminder
import io.rebble.libpebblecommon.calendar.NewCalendarEvent
import io.rebble.libpebblecommon.calendar.SystemCalendar
import io.rebble.libpebblecommon.database.entity.CalendarEntity
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlin.time.Instant

/** Read-only Sailfish calendar source backed by the isolated mkcal helper. */
internal class PlatformSystemCalendar(
    private val controller: PlatformProviderController,
) : SystemCalendar {
    private val changes = MutableSharedFlow<Unit>(extraBufferCapacity = 1)

    init {
        controller.addCalendarChangedListener { changes.tryEmit(Unit) }
    }

    override suspend fun getCalendars(): List<CalendarEntity> {
        val records = queryAll(PlatformProviderController.CALENDAR_QUERY_CALENDARS)
            .flatMap { it.calendars }
        return records.map { calendar ->
            CalendarEntity(
                platformId = calendar.id,
                name = calendar.name,
                ownerName = calendar.ownerName,
                ownerId = calendar.ownerId,
                color = calendar.colorArgb,
                enabled = calendar.flags and CALENDAR_ENABLED != 0,
                syncEvents = calendar.flags and CALENDAR_SYNC_EVENTS != 0,
                visible = calendar.flags and CALENDAR_VISIBLE != 0,
            )
        }
    }

    override suspend fun getCalendarEvents(
        calendar: CalendarEntity,
        startDate: Instant,
        endDate: Instant,
    ): List<CalendarEvent> = queryAll(
        kind = PlatformProviderController.CALENDAR_QUERY_EVENTS,
        startMs = startDate.toEpochMilliseconds(),
        endMs = endDate.toEpochMilliseconds(),
        calendarId = calendar.platformId,
    ).flatMap { it.events }.map { event ->
        CalendarEvent(
            id = event.id,
            calendarId = event.calendarId,
            title = event.title,
            description = event.description,
            location = event.location.ifEmpty { null },
            startTime = Instant.fromEpochMilliseconds(event.startMs),
            endTime = Instant.fromEpochMilliseconds(event.endMs),
            allDay = event.flags and EVENT_ALL_DAY != 0,
            attendees = event.attendees.map { attendee ->
                EventAttendee(
                    name = attendee.name.ifEmpty { null },
                    email = attendee.email.ifEmpty { null },
                    role = EventAttendee.Role.entries.getOrNull(attendee.role),
                    isOrganizer = attendee.flags and ATTENDEE_ORGANIZER != 0,
                    isCurrentUser = attendee.flags and ATTENDEE_CURRENT_USER != 0,
                    attendanceStatus =
                        EventAttendee.AttendanceStatus.entries.getOrNull(attendee.status),
                )
            },
            recurs = event.flags and EVENT_RECURS != 0,
            reminders = event.reminderMinutes.map(::EventReminder),
            availability = CalendarEvent.Availability.entries[event.availability],
            status = CalendarEvent.Status.entries[event.status],
            baseEventId = event.baseEventId,
        )
    }

    private suspend fun queryAll(
        kind: Int,
        startMs: Long = 0,
        endMs: Long = 0,
        calendarId: String = "",
    ): List<PlatformCalendarSnapshot> {
        val pages = mutableListOf<PlatformCalendarSnapshot>()
        var offset = 0
        do {
            val result = controller.queryCalendarPage(
                kind = kind,
                maxRecords = PlatformProviderController.CALENDAR_PAGE_MAX,
                offset = offset,
                startMs = startMs,
                endMs = endMs,
                calendarId = calendarId,
            )
            val snapshot = when (result) {
                is PlatformCalendarQueryResult.Success -> result.snapshot
                is PlatformCalendarQueryResult.Error ->
                    throw IllegalStateException("Calendar provider error ${result.status}")
            }
            check(snapshot.kind == kind) { "Calendar provider returned the wrong query kind" }
            pages += snapshot
            val next = snapshot.nextOffset
            check(next == 0 || next > offset) { "Calendar provider pagination did not advance" }
            offset = next
        } while (offset != 0)
        return pages
    }

    override suspend fun enableSyncForCalendar(calendar: CalendarEntity) = Unit

    override fun registerForCalendarChanges(): Flow<Unit> = changes

    override fun hasPermission(): Boolean =
        controller.snapshot().domains and PlatformProviderController.CALENDAR_DOMAIN != 0L

    override suspend fun createEvent(event: NewCalendarEvent): String? = null

    override fun supportsPinActions(): Boolean = false

    private companion object {
        const val CALENDAR_VISIBLE = 1
        const val CALENDAR_ENABLED = 1 shl 1
        const val CALENDAR_SYNC_EVENTS = 1 shl 2
        const val EVENT_ALL_DAY = 1
        const val EVENT_RECURS = 1 shl 1
        const val ATTENDEE_ORGANIZER = 1
        const val ATTENDEE_CURRENT_USER = 1 shl 1
    }
}
