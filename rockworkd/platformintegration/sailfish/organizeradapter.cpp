#include "organizeradapter.h"

#include <QOrganizerItemFetchRequest>
#include <QDebug>
#include <QOrganizerEventOccurrence>
#include <QOrganizerItemDetail>
#include <extendedcalendar.h>
#include <extendedstorage.h>

QTORGANIZER_USE_NAMESPACE

#define MANAGER           "eds"
#define MANAGER_FALLBACK  "memory"

OrganizerAdapter::OrganizerAdapter(QObject *parent) : QObject(parent),
    _calendar(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone())),
    _calendarStorage(_calendar->defaultStorage(_calendar)),
    _refreshTimer(new QTimer(this))
{
    _refreshTimer->setSingleShot(true);
    _refreshTimer->setInterval(2000);
    connect(_refreshTimer, &QTimer::timeout,
            this, &OrganizerAdapter::refresh);

    if (_calendarStorage->open()) {
        scheduleRefresh();
    } else {
        qWarning() << "Cannot open calendar database";
    }
}

QString OrganizerAdapter::normalizeCalendarName(QString name)
{
    if (name == "qtn_caln_personal_caln") {
        return tr("Personal");
    }

    return name;
}

void OrganizerAdapter::scheduleRefresh()
{
    if (!_refreshTimer->isActive()) {
        _refreshTimer->start();
    }
}

void OrganizerAdapter::refresh()
{
    QList<CalendarEvent> items;

    QDate today = QDate::currentDate();
    QDate endDate = today.addDays(7);
    _calendarStorage->loadRecurringIncidences();
    _calendarStorage->load(today, endDate);

    auto events = _calendar->rawExpandedEvents(today, endDate, true, true);
    for (const auto &expanded : events) {
        const QDateTime &start = expanded.first.dtStart;
        const QDateTime &end = expanded.first.dtEnd;
        KCalCore::Incidence::Ptr incidence = expanded.second;

        CalendarEvent event;
        event.setId(incidence->uid());
        event.setTitle(incidence->summary());
        event.setDescription(incidence->description());
        event.setStartTime(start);
        event.setEndTime(end);
        event.setIsAllDay(incidence->allDay());
        if (!incidence->location().isEmpty()) {
            event.setLocation(incidence->location());
        }
        mKCal::Notebook::Ptr notebook = _calendarStorage->notebook(_calendar->notebook(incidence));
        if (notebook) {
            event.setCalendar(normalizeCalendarName(notebook->name()));
        }
        event.setComment(incidence->comments().join(";"));

        QStringList attendees;
        foreach (const QSharedPointer<KCalCore::Attendee> attendee, incidence->attendees()) {
            attendees.append(attendee->fullName());
        }
        event.setGuests(attendees);
        event.setRecurring(event.recurring());

        items.append(event);
    }

    if (m_items != items) {
        m_items = items;
        emit itemsChanged(m_items);
    }

}

void OrganizerAdapter::storageModified(mKCal::ExtendedStorage *storage, const QString &info)
{
    Q_UNUSED(storage);
    qDebug() << "Storage modified:" << info;
    scheduleRefresh();
}

void OrganizerAdapter::storageProgress(mKCal::ExtendedStorage *storage, const QString &info)
{
    Q_UNUSED(storage);
    Q_UNUSED(info);
    // Nothing to do
}

void OrganizerAdapter::storageFinished(mKCal::ExtendedStorage *storage, bool error, const QString &info)
{
    Q_UNUSED(storage);
    Q_UNUSED(error);
    Q_UNUSED(info);
    // Nothing to do
}

QList<CalendarEvent> OrganizerAdapter::items() const
{
    return m_items;
}
