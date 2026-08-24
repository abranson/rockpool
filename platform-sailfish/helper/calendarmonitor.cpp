/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calendarmonitor.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QSharedPointer>
#include <QStringList>
#include <QTimeZone>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Attendee>
#include <KCalendarCore/Event>
#include <KCalendarCore/OccurrenceIterator>
#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <extendedstorageobserver.h>
#include <notebook.h>

#include <algorithm>
#include <vector>

#include "libpebble3d-platform.h"

namespace {

std::string boundedUtf8(const QString &value, size_t maximum) {
    QByteArray bytes = value.toUtf8();
    if (bytes.size() > static_cast<int>(maximum)) {
        bytes.truncate(static_cast<int>(maximum));
        while (!bytes.isEmpty() && !lp3wire::validUtf8(std::string(
                   bytes.constData(), static_cast<size_t>(bytes.size())))) {
            bytes.chop(1);
        }
    }
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QString normalizedName(const QString &name) {
    return name == QStringLiteral("qtn_caln_personal_caln") ?
        QStringLiteral("Personal") : name;
}

uint32_t parseColor(const QString &text) {
    QString color = text.trimmed();
    if (color.startsWith(QLatin1Char('#'))) {
        color.remove(0, 1);
    }
    bool valid = false;
    const uint value = color.toUInt(&valid, 16);
    if (valid && color.size() == 6) {
        return UINT32_C(0xff000000) | value;
    }
    if (valid && color.size() == 8) {
        return value;
    }
    return UINT32_C(0xff0099cc);
}

uint32_t attendeeRole(KCalendarCore::Attendee::Role role) {
    switch (role) {
    case KCalendarCore::Attendee::ReqParticipant:
        return 1;
    case KCalendarCore::Attendee::OptParticipant:
        return 2;
    case KCalendarCore::Attendee::NonParticipant:
        return 3;
    default:
        return 0;
    }
}

uint32_t attendeeStatus(KCalendarCore::Attendee::PartStat status) {
    switch (status) {
    case KCalendarCore::Attendee::Accepted:
        return 1;
    case KCalendarCore::Attendee::Declined:
        return 2;
    case KCalendarCore::Attendee::NeedsAction:
        return 3;
    case KCalendarCore::Attendee::Tentative:
        return 4;
    default:
        return 0;
    }
}

uint32_t eventStatus(KCalendarCore::Incidence::Status status) {
    switch (status) {
    case KCalendarCore::Incidence::StatusConfirmed:
        return 1;
    case KCalendarCore::Incidence::StatusCanceled:
        return 2;
    case KCalendarCore::Incidence::StatusTentative:
        return 3;
    default:
        return 0;
    }
}

bool eventLess(const lp3wire::CalendarEventData &left,
               const lp3wire::CalendarEventData &right) {
    return left.startMs < right.startMs ||
           (left.startMs == right.startMs && left.id < right.id);
}

bool calendarLess(const lp3wire::CalendarData &left,
                  const lp3wire::CalendarData &right) {
    return left.id < right.id;
}

} // namespace

class CalendarCancelState {
public:
    void cancel(quint64 requestId) {
        QMutexLocker locker(&mutex);
        cancelled.insert(requestId);
    }

    bool isCancelled(quint64 requestId) {
        QMutexLocker locker(&mutex);
        return cancelled.contains(requestId);
    }

    void retire(quint64 requestId) {
        QMutexLocker locker(&mutex);
        cancelled.remove(requestId);
    }

private:
    QMutex mutex;
    QSet<quint64> cancelled;
};

class CalendarWorker : public QObject,
                       public mKCal::ExtendedStorageObserver {
    Q_OBJECT

public:
    explicit CalendarWorker(CalendarCancelState *cancelState)
        : m_cancelState(cancelState), m_ready(false) {}

public slots:
    void start() {
        if (m_ready) {
            return;
        }
        m_calendar = mKCal::ExtendedCalendar::Ptr(
            new mKCal::ExtendedCalendar(QTimeZone::systemTimeZone()));
        m_storage = m_calendar->defaultStorage(m_calendar);
        if (!m_storage || !m_storage->open()) {
            m_storage.clear();
            m_calendar.clear();
            emit healthChanged(false);
            return;
        }
        m_storage->registerObserver(this);
        m_ready = true;
        emit healthChanged(true);
    }

    void stop() {
        if (m_storage) {
            m_storage->unregisterObserver(this);
            m_storage->close();
        }
        m_ready = false;
        m_storage.clear();
        m_calendar.clear();
        emit stopped();
    }

    void query(quint64 requestId, quint32 kind, quint32 maxRecords,
               quint32 offset, qint64 startMs, qint64 endMs,
               const QString &calendarId) {
        lp3wire::CalendarReplyData reply;
        reply.kind = kind;
        reply.nextOffset = 0;
        int32_t status = LP3_PLATFORM_OK;

        if (m_cancelState->isCancelled(requestId)) {
            m_cancelState->retire(requestId);
            return;
        }
        if (!m_ready || !m_storage || !m_calendar) {
            status = LP3_PLATFORM_UNAVAILABLE;
        } else if (kind == lp3wire::CalendarQueryCalendars) {
            status = queryCalendars(requestId, maxRecords, offset, &reply);
        } else {
            status = queryEvents(requestId, maxRecords, offset, startMs, endMs,
                                 calendarId, &reply);
        }
        if (m_cancelState->isCancelled(requestId)) {
            m_cancelState->retire(requestId);
            return;
        }
        if (status != LP3_PLATFORM_OK) {
            reply.nextOffset = 0;
            reply.calendars.clear();
            reply.events.clear();
        }
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeCalendarReply(
                static_cast<uint32_t>(status), reply, &payload)) {
            reply.nextOffset = 0;
            reply.calendars.clear();
            reply.events.clear();
            payload.clear();
            lp3wire::encodeCalendarReply(
                LP3_PLATFORM_PROTOCOL_ERROR, reply, &payload);
            status = LP3_PLATFORM_PROTOCOL_ERROR;
        }
        const QByteArray bytes(
            reinterpret_cast<const char *>(&payload[0]),
            static_cast<int>(payload.size()));
        m_cancelState->retire(requestId);
        emit completed(requestId, status, bytes);
    }

signals:
    void completed(quint64 requestId, int status, const QByteArray &payload);
    void calendarChanged();
    void healthChanged(bool ready);
    void stopped();

protected:
    void storageModified(mKCal::ExtendedStorage *, const QString &) Q_DECL_OVERRIDE {
        emit calendarChanged();
    }

    void storageFinished(mKCal::ExtendedStorage *, bool error,
                         const QString &) Q_DECL_OVERRIDE {
        if (error && m_ready) {
            m_ready = false;
            emit healthChanged(false);
        }
    }

private:
    int32_t queryCalendars(quint64 requestId, quint32 maxRecords,
                           quint32 offset,
                           lp3wire::CalendarReplyData *reply) {
        QSettings settings(QStringLiteral("nemo"),
                           QStringLiteral("nemo-qml-plugin-calendar"));
        const mKCal::Notebook::List notebooks = m_storage->notebooks();
        std::vector<lp3wire::CalendarData> all;
        for (mKCal::Notebook::List::const_iterator it = notebooks.constBegin();
             it != notebooks.constEnd(); ++it) {
            if (m_cancelState->isCancelled(requestId)) {
                return LP3_PLATFORM_CANCELLED;
            }
            const mKCal::Notebook::Ptr notebook = *it;
            if (!notebook || notebook->uid().isEmpty()) {
                continue;
            }
            const QString id = notebook->uid();
            const bool excluded = settings.value(
                QStringLiteral("exclude/") + id, false).toBool();
            lp3wire::CalendarData calendar;
            calendar.flags = (notebook->isVisible() ?
                                  lp3wire::CalendarVisible : 0) |
                (!excluded ? lp3wire::CalendarEnabled : 0) |
                lp3wire::CalendarSyncEvents;
            calendar.colorArgb = parseColor(settings.value(
                QStringLiteral("colors/") + id,
                notebook->color()).toString());
            calendar.id = boundedUtf8(id, lp3wire::kCalendarIdMax);
            calendar.name = boundedUtf8(
                normalizedName(notebook->name()), lp3wire::kCalendarNameMax);
            if (calendar.name.empty()) {
                calendar.name = calendar.id;
            }
            calendar.ownerName = boundedUtf8(
                notebook->name(), lp3wire::kCalendarOwnerMax);
            calendar.ownerId = boundedUtf8(
                notebook->account(), lp3wire::kCalendarOwnerMax);
            all.push_back(calendar);
            if (all.size() > lp3wire::kCalendarTotalMax) {
                return LP3_PLATFORM_BUSY;
            }
        }
        std::sort(all.begin(), all.end(), calendarLess);
        const size_t begin = qMin(static_cast<size_t>(offset), all.size());
        const size_t end = qMin(begin + static_cast<size_t>(maxRecords),
                                all.size());
        reply->calendars.assign(all.begin() + begin, all.begin() + end);
        reply->nextOffset = end < all.size() ? static_cast<uint32_t>(end) : 0;
        return LP3_PLATFORM_OK;
    }

    int32_t queryEvents(quint64 requestId, quint32 maxRecords, quint32 offset,
                        qint64 startMs, qint64 endMs,
                        const QString &calendarId,
                        lp3wire::CalendarReplyData *reply) {
        const QDateTime start = QDateTime::fromMSecsSinceEpoch(startMs);
        const QDateTime end = QDateTime::fromMSecsSinceEpoch(endMs);
        if (!start.isValid() || !end.isValid() ||
            !m_storage->load(start.date(), end.date().addDays(1))) {
            return LP3_PLATFORM_UNAVAILABLE;
        }
        const mKCal::Notebook::Ptr notebook = m_storage->notebook(calendarId);
        if (!notebook) {
            return LP3_PLATFORM_INVALID_ARGUMENT;
        }
        QSettings settings(QStringLiteral("nemo"),
                           QStringLiteral("nemo-qml-plugin-calendar"));
        if (settings.value(QStringLiteral("exclude/") + calendarId,
                           false).toBool()) {
            return LP3_PLATFORM_OK;
        }

        std::vector<lp3wire::CalendarEventData> all;
        KCalendarCore::OccurrenceIterator occurrences(*m_calendar, start, end);
        while (occurrences.hasNext()) {
            occurrences.next();
            if (m_cancelState->isCancelled(requestId)) {
                return LP3_PLATFORM_CANCELLED;
            }
            const KCalendarCore::Incidence::Ptr incidence = occurrences.incidence();
            if (!incidence ||
                incidence->type() != KCalendarCore::IncidenceBase::TypeEvent ||
                !m_calendar->isVisible(incidence) ||
                m_calendar->notebook(incidence) != calendarId) {
                continue;
            }
            const KCalendarCore::Event::Ptr event =
                incidence.dynamicCast<KCalendarCore::Event>();
            if (!event) {
                continue;
            }
            const QDateTime occurrenceStart = occurrences.occurrenceStartDate();
            qint64 durationSeconds = event->dtStart().secsTo(event->dtEnd());
            if (durationSeconds <= 0) {
                durationSeconds = incidence->allDay() ? 24 * 60 * 60 : 60 * 60;
            }

            lp3wire::CalendarEventData data;
            data.flags = (incidence->allDay() ?
                              lp3wire::CalendarEventAllDay : 0) |
                (incidence->recurs() ? lp3wire::CalendarEventRecurs : 0);
            data.availability = event->transparency() ==
                    KCalendarCore::Event::Transparent ? 0 : 1;
            data.status = eventStatus(incidence->status());
            data.startMs = occurrenceStart.toMSecsSinceEpoch();
            data.endMs = occurrenceStart.addSecs(durationSeconds)
                             .toMSecsSinceEpoch();
            const QString occurrenceId = incidence->uid() +
                QLatin1Char('#') + QString::number(data.startMs);
            data.id = boundedUtf8(
                occurrenceId, lp3wire::kCalendarEventIdMax);
            data.calendarId = boundedUtf8(
                calendarId, lp3wire::kCalendarIdMax);
            data.baseEventId = boundedUtf8(
                incidence->uid(), lp3wire::kCalendarEventIdMax);
            data.title = boundedUtf8(
                incidence->summary(), lp3wire::kCalendarTitleMax);
            if (data.title.empty()) {
                data.title = "(No title)";
            }
            data.description = boundedUtf8(
                incidence->description(), lp3wire::kCalendarDescriptionMax);
            data.location = boundedUtf8(
                incidence->location(), lp3wire::kCalendarLocationMax);

            const KCalendarCore::Attendee::List attendees =
                incidence->attendees();
            const QString organizerEmail = incidence->organizer().email();
            for (KCalendarCore::Attendee::List::const_iterator it =
                     attendees.constBegin();
                 it != attendees.constEnd() &&
                     data.attendees.size() < lp3wire::kCalendarAttendeeMax;
                 ++it) {
                lp3wire::CalendarAttendeeData attendee;
                attendee.flags = !organizerEmail.isEmpty() &&
                    it->email().compare(organizerEmail,
                                        Qt::CaseInsensitive) == 0 ?
                    lp3wire::CalendarAttendeeOrganizer : 0;
                attendee.flags |= it->email().compare(
                    notebook->account(), Qt::CaseInsensitive) == 0 &&
                    !notebook->account().isEmpty() ?
                    lp3wire::CalendarAttendeeCurrentUser : 0;
                attendee.role = attendeeRole(it->role());
                attendee.status = attendeeStatus(it->status());
                attendee.name = boundedUtf8(
                    it->fullName(), lp3wire::kCalendarAttendeeTextMax);
                attendee.email = boundedUtf8(
                    it->email(), lp3wire::kCalendarAttendeeTextMax);
                if (!attendee.name.empty() || !attendee.email.empty()) {
                    data.attendees.push_back(attendee);
                }
            }
            const KCalendarCore::Alarm::List alarms = incidence->alarms();
            for (KCalendarCore::Alarm::List::const_iterator it =
                     alarms.constBegin();
                 it != alarms.constEnd() &&
                     data.reminderMinutes.size() < lp3wire::kCalendarReminderMax;
                 ++it) {
                const KCalendarCore::Alarm::Ptr alarm = *it;
                if (alarm && alarm->enabled() && alarm->hasStartOffset()) {
                    const qint64 offsetSeconds = alarm->startOffset().asSeconds();
                    if (offsetSeconds <= 0) {
                        data.reminderMinutes.push_back(static_cast<int32_t>(
                            qMin<qint64>(-offsetSeconds / 60,
                                       366LL * 24 * 60)));
                    }
                }
            }
            all.push_back(data);
            if (all.size() > lp3wire::kCalendarTotalMax) {
                return LP3_PLATFORM_BUSY;
            }
        }
        std::sort(all.begin(), all.end(), eventLess);
        const size_t begin = qMin(static_cast<size_t>(offset), all.size());
        const size_t sliceEnd = qMin(
            begin + static_cast<size_t>(maxRecords), all.size());
        reply->events.assign(all.begin() + begin, all.begin() + sliceEnd);
        reply->nextOffset = sliceEnd < all.size() ?
            static_cast<uint32_t>(sliceEnd) : 0;
        return LP3_PLATFORM_OK;
    }

    CalendarCancelState *m_cancelState;
    bool m_ready;
    mKCal::ExtendedCalendar::Ptr m_calendar;
    mKCal::ExtendedStorage::Ptr m_storage;
};

CalendarMonitor::CalendarMonitor(const CompletedCallback &completed,
                                 const ChangedCallback &changed,
                                 const HealthCallback &health,
                                 QObject *parent)
    : QObject(parent), m_worker(NULL),
      m_cancelState(new CalendarCancelState), m_completed(completed),
      m_changed(changed), m_health(health), m_started(false), m_ready(false) {
    m_worker = new CalendarWorker(m_cancelState);
    m_worker->moveToThread(&m_thread);
    QObject::connect(this, &CalendarMonitor::startRequested,
                     m_worker, &CalendarWorker::start);
    QObject::connect(this, &CalendarMonitor::queryRequested,
                     m_worker, &CalendarWorker::query);
    QObject::connect(m_worker, &CalendarWorker::completed, this,
                     [this](quint64 requestId, int status,
                            const QByteArray &payload) {
        m_completed(requestId, static_cast<int32_t>(status), payload);
    });
    QObject::connect(m_worker, &CalendarWorker::calendarChanged, this,
                     [this]() { m_changed(); });
    QObject::connect(m_worker, &CalendarWorker::healthChanged, this,
                     [this](bool ready) {
        m_ready = ready;
        m_health(ready);
    });
    QObject::connect(this, &CalendarMonitor::stopRequested,
                     m_worker, &CalendarWorker::stop);
    QObject::connect(m_worker, &CalendarWorker::stopped,
                     &m_thread, &QThread::quit);
}

CalendarMonitor::~CalendarMonitor() {
    if (m_started) {
        emit stopRequested();
        m_thread.wait();
    }
    delete m_worker;
    delete m_cancelState;
}

bool CalendarMonitor::start() {
    if (!m_started) {
        m_started = true;
        m_thread.start();
        emit startRequested();
    }
    return m_ready;
}

void CalendarMonitor::query(quint64 requestId,
                            const lp3wire::CalendarQueryData &query) {
    emit queryRequested(requestId, query.kind, query.maxRecords, query.offset,
                        query.startMs, query.endMs,
                        QString::fromUtf8(
                            query.calendarId.data(),
                            static_cast<int>(query.calendarId.size())));
}

bool CalendarMonitor::cancel(quint64 requestId) {
    m_cancelState->cancel(requestId);
    return true;
}

#include "calendarmonitor.moc"
