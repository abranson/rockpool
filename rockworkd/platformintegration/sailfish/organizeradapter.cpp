#include "organizeradapter.h"

#include <QOrganizerItemFetchRequest>
#include <QDebug>
#include <QOrganizerEventOccurrence>
#include <QOrganizerItemDetail>
#   include <extendedcalendar.h>
#   include <extendedstorage.h>

QTORGANIZER_USE_NAMESPACE

#define MANAGER           "eds"
#define MANAGER_FALLBACK  "memory"

OrganizerAdapter::OrganizerAdapter(QObject *parent) : QObject(parent)
{
    QString envManager(qgetenv("ALARM_BACKEND"));
    if (envManager.isEmpty())
        envManager = MANAGER;
    if (!QOrganizerManager::availableManagers().contains(envManager)) {
        envManager = MANAGER_FALLBACK;
    }
    m_manager = new QOrganizerManager(envManager);
    m_manager->setParent(this);
    connect(m_manager, &QOrganizerManager::dataChanged, this, &OrganizerAdapter::refresh);

    // This is the snippet from Fahrplan that enumerates the calendars, but here it doesn't seem to have permission to access the db
    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr ( new mKCal::ExtendedCalendar( QLatin1String( "UTC" ) ) );
    mKCal::ExtendedStorage::Ptr storage = mKCal::ExtendedCalendar::defaultStorage( calendar );
    if (storage->open()) {
        mKCal::Notebook::List notebooks = storage->notebooks();
        qDebug()<< "Notebooks: " + notebooks.count();
        for (int ii = 0; ii < notebooks.count(); ++ii) {
            if (!notebooks.at(ii)->isReadOnly()) {
                m_calendars << CalendarInfo(normalizeCalendarName(notebooks.at(ii)->name()), notebooks.at(ii)->uid());
                qDebug()<< "Notebook: " << notebooks.at(ii)->name() << notebooks.at(ii)->uid();
            }
        }
    }
}

QString OrganizerAdapter::normalizeCalendarName(QString name)
{
    if (name == "qtn_caln_personal_caln") {
        return tr("Personal");
    }

    return name;
}

void OrganizerAdapter::refresh()
{
    QList<CalendarEvent> items;
    foreach (const QOrganizerItem &item, m_manager->items()) {
        QOrganizerEvent organizerEvent(item);
        if (organizerEvent.displayLabel().isEmpty()) {
            continue;
        }
        CalendarEvent event;
        event.setId(organizerEvent.id().toString());
        event.setTitle(organizerEvent.displayLabel());
        event.setDescription(organizerEvent.description());
        event.setStartTime(organizerEvent.startDateTime());
        event.setEndTime(organizerEvent.endDateTime());
        event.setLocation(organizerEvent.location());
        event.setComment(organizerEvent.comments().join(";"));
        QStringList attendees;
        foreach (const QOrganizerItemDetail &attendeeDetail, organizerEvent.details(QOrganizerItemDetail::TypeEventAttendee)) {
            attendees.append(attendeeDetail.value(QOrganizerItemDetail::TypeEventAttendee + 1).toString());
        }
        event.setGuests(attendees);
        event.setRecurring(organizerEvent.recurrenceRules().count() > 0);

        items.append(event);

        quint64 startTimestamp = QDateTime::currentMSecsSinceEpoch();
        startTimestamp -= (1000 * 60 * 60 * 24 * 7);

        foreach (const QOrganizerItem &occurranceItem, m_manager->itemOccurrences(item, QDateTime::fromMSecsSinceEpoch(startTimestamp), QDateTime::currentDateTime().addDays(7))) {
            QOrganizerEventOccurrence organizerOccurrance(occurranceItem);
            event.setId(organizerOccurrance.id().toString());
            event.setStartTime(organizerOccurrance.startDateTime());
            event.setEndTime(organizerOccurrance.endDateTime());
            items.append(event);
        }
    }

    if (m_items != items) {
        m_items = items;
        emit itemsChanged(m_items);
    }

}

QList<CalendarEvent> OrganizerAdapter::items() const
{
    return m_items;
}
