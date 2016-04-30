#include "timelinemanager.h"
#include "blobdb.h"

#include "watchdatareader.h"
#include "watchdatawriter.h"


#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QDir>

#include <libintl.h>

TimelineManager::TimelineManager(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointActionHandler, this, "actionInvoked");
    m_timelineStoragePath = pebble->storagePath() + "/blobdb/"; // yeah, that sucks, migration...
    QDir dir(m_timelineStoragePath);
    if (!dir.exists() && !dir.mkpath(m_timelineStoragePath)) {
        qWarning() << "Error creating timeline storage dir.";
        return;
    }
    dir.setNameFilters({"calendarevent-*"});
    foreach (const QFileInfo &fi, dir.entryInfoList()) {
        CalendarEvent event;
        event.loadFromCache(m_timelineStoragePath, fi.fileName().right(QUuid().toString().length()));

        m_calendarEntries.append(event);
    }
}

void TimelineManager::actionInvoked(const QByteArray &actionReply)
{
    WatchDataReader reader(actionReply);
    TimelineAction::Type actionType = (TimelineAction::Type)reader.read<quint8>();
    QUuid notificationId = reader.readUuid();
    quint8 actionId = reader.read<quint8>();
    quint8 param = reader.read<quint8>(); // Is this correct? So far I've only seen 0x00 in here

    // Not sure what to do with those yet
    Q_UNUSED(actionType)
    Q_UNUSED(param)

    qDebug() << "Action invoked" << actionId << actionType << actionReply.toHex() << param;

    BlobDB::Status status = BlobDB::StatusError;
    QList<TimelineAttribute> attributes;

    Notification notification = m_notificationSources.value(notificationId);
    QString sourceId = notification.sourceId();
    if (sourceId.isEmpty()) {
        status = BlobDB::StatusError;
    } else {
        switch (actionId) {
        case 0: { // Dismiss
            TimelineAttribute textAttribute(TimelineAttribute::TypeSubtitle, "Dismissed!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(TimelineAttribute::TypeLargeIcon, TimelineAttribute::IconIDResultDismissed);
            attributes.append(iconAttribute);
            emit removeNotification(notification.uuid());
            status = BlobDB::StatusSuccess;
            break;
        }
        case 1: { // Mute source
            TimelineAttribute textAttribute(TimelineAttribute::TypeSubtitle, "Muted!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(TimelineAttribute::TypeLargeIcon, TimelineAttribute::IconIDResultMute);
            attributes.append(iconAttribute);
            emit muteSource(sourceId);
            status = BlobDB::StatusSuccess;
            break;
        }
        case 2: { // Open on phone
            TimelineAttribute textAttribute(TimelineAttribute::TypeSubtitle, "Opened!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(TimelineAttribute::TypeLargeIcon, TimelineAttribute::IconIDResultSent);
            attributes.append(iconAttribute);
            qDebug() << "opening" << notification.actToken();
            emit actionTriggered(notification.uuid(), notification.actToken());
            status = BlobDB::StatusSuccess;
            break;
        }
        default: { // Dunno lol
            TimelineAttribute textAttribute(TimelineAttribute::TypeSubtitle, "Action failed!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(TimelineAttribute::TypeLargeIcon, TimelineAttribute::IconIDResultFailed);
            attributes.append(iconAttribute);
            status = BlobDB::StatusError;
        }
        }
    }

    QByteArray reply;
    reply.append(0x11); // Length of id & status code
    reply.append(notificationId.toRfc4122());
    reply.append(status);
    reply.append(attributes.count());
    foreach (const TimelineAttribute &attrib, attributes) {
        reply.append(attrib.serialize());
    }
    m_connection->writeToPebble(WatchConnection::EndpointActionHandler, reply);
}

void TimelineManager::sendNotification(const Notification &notification)
{
    qDebug() << "inserting notification into blobdb:" << notification.type();
    TimelineAttribute::IconID iconId = TimelineAttribute::IconIDNotificationGeneric;
    TimelineAttribute::Color color = TimelineAttribute::ColorRed;
    QString muteName;
    switch (notification.type()) {
    case Notification::NotificationTypeFacebook:
        iconId = TimelineAttribute::IconIDNotificationFacebook;
        color = TimelineAttribute::ColorBlue;
        muteName = "Facebook";
        break;
    case Notification::NotificationTypeFlag:
        iconId = TimelineAttribute::IconIDNotificationFlag;
        break;
    case Notification::NotificationTypeGeneric:
        iconId = TimelineAttribute::IconIDNotificationGeneric;
        break;
    case Notification::NotificationTypeGMail:
        iconId = TimelineAttribute::IconIDNotificationGMail;
        muteName = "GMail";
        break;
    case Notification::NotificationTypeHangout:
        iconId = TimelineAttribute::IconIDNotificationGoogleHangouts;
        color = TimelineAttribute::ColorGreen;
        muteName = "Google Hangout";
        break;
    case Notification::NotificationTypeMissedCall:
        iconId = TimelineAttribute::IconIDTimelineMissedCall;
        muteName = gettext("call notifications");
        break;
    case Notification::NotificationTypeMusic:
        iconId = TimelineAttribute::IconIDAudioCasette;
        break;
    case Notification::NotificationTypeReminder:
        iconId = TimelineAttribute::IconIDTimelineCalendar;
        // TRANSLATORS: notifications for calendar reminders
        muteName = gettext("reminders");
        break;
    case Notification::NotificationTypeTelegram:
        iconId = TimelineAttribute::IconIDNotificationTelegram;
        color = TimelineAttribute::ColorLightBlue;
        muteName = "Telegram";
        break;
    case Notification::NotificationTypeTwitter:
        iconId = TimelineAttribute::IconIDNotificationTwitter;
        color = TimelineAttribute::ColorBlue2;
        muteName = "Twitter";
        break;
    case Notification::NotificationTypeWeather:
        iconId = TimelineAttribute::IconIDTimelineWeather;
        muteName = gettext("weather notifications");
        break;
    case Notification::NotificationTypeWhatsApp:
        iconId = TimelineAttribute::IconIDNotificationWhatsapp;
        color = TimelineAttribute::ColorGreen;
        muteName = "WhatsApp";
        break;
    case Notification::NotificationTypeSMS:
        muteName = gettext("SMS");
        iconId = TimelineAttribute::IconIDGenericSMS;
        break;
    case Notification::NotificationTypeEmail:
        muteName = gettext("e mails");
        iconId = TimelineAttribute::IconIDGenericEmail;
        break;
    default:
        iconId = TimelineAttribute::IconIDNotificationGeneric;
    }

    QUuid itemUuid = notification.uuid();
    TimelineItem timelineItem(itemUuid, TimelineItem::TypeNotification);
    timelineItem.setFlags(TimelineItem::FlagSingleEvent);

    timelineItem.setParentId(QUuid("ed429c16-f674-4220-95da-454f303f15e2"));

    TimelineAttribute titleAttribute(TimelineAttribute::TypeTitle, notification.sender().remove(QRegExp("<[^>]*>")).left(64).toUtf8());
    timelineItem.appendAttribute(titleAttribute);

    TimelineAttribute subjectAttribute(TimelineAttribute::TypeSubtitle, notification.subject().remove(QRegExp("<[^>]*>")).left(64).toUtf8());
    timelineItem.appendAttribute(subjectAttribute);

    TimelineAttribute bodyAttribute(TimelineAttribute::TypeBody, notification.body().remove(QRegExp("<[^>]*>")).toUtf8());
    timelineItem.appendAttribute(bodyAttribute);

    TimelineAttribute tinyIconAttribute(TimelineAttribute::TypeTinyIcon, iconId);
    timelineItem.appendAttribute(tinyIconAttribute);

    TimelineAttribute smallIconAttribute(TimelineAttribute::TypeSmallIcon, iconId);
    timelineItem.appendAttribute(smallIconAttribute);

    TimelineAttribute largeIconAttribute(TimelineAttribute::TypeLargeIcon, iconId);
    timelineItem.appendAttribute(largeIconAttribute);

    TimelineAttribute iconAttribute(TimelineAttribute::TypeTinyIcon, iconId);
    timelineItem.appendAttribute(iconAttribute);

    TimelineAttribute colorAttribute(TimelineAttribute::TypeColor, color);
    timelineItem.appendAttribute(colorAttribute);

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(TimelineAttribute::TypeTitle, gettext("Dismiss"));
    dismissAction.appendAttribute(dismissAttribute);
    timelineItem.appendAction(dismissAction);

    if (!muteName.isEmpty()) {
        TimelineAction muteAction(1, TimelineAction::TypeGeneric);

        TimelineAttribute muteActionAttribute(TimelineAttribute::TypeTitle, QString::fromUtf8(gettext("Mute %1")).arg(muteName).toUtf8());
        muteAction.appendAttribute(muteActionAttribute);
        timelineItem.appendAction(muteAction);
    }

    if (!notification.actToken().isEmpty()) {
        TimelineAction actAction(2, TimelineAction::TypeGeneric);
        TimelineAttribute actActionAttribute(TimelineAttribute::TypeTitle, gettext("Open on phone"));
        actAction.appendAttribute(actActionAttribute);
        timelineItem.appendAction(actAction);
    }

    m_pebble->blobdb()->insert(BlobDB::BlobDBIdNotification, timelineItem);
    m_notificationSources.insert(itemUuid, notification);
}

void TimelineManager::insertTimelinePin(const QUuid &uuid, TimelineItem::Layout layout, bool isAllDay, const QDateTime &startTime, const QDateTime &endTime, const QString &title, const QString &description, const QMap<QString, QString> fields, bool recurring)
{
//    TimelineItem item(TimelineItem::TypePin, TimelineItem::FlagSingleEvent, QDateTime::currentDateTime().addMSecs(1000 * 60 * 2), 60);

    qDebug() << "inserting timeline pin:" << title << startTime << endTime;
    int duration = (endTime.toMSecsSinceEpoch() - startTime.toMSecsSinceEpoch()) / 1000 / 60;
    TimelineItem::Flag flag = isAllDay ? TimelineItem::FlagAllDay : TimelineItem::FlagSingleEvent;
    TimelineItem item(uuid, TimelineItem::TypePin, flag, startTime, duration);
    item.setLayout(layout);

    TimelineAttribute titleAttribute(TimelineAttribute::TypeTitle, title.toUtf8());
    item.appendAttribute(titleAttribute);

    if (!description.isEmpty()) {
        TimelineAttribute bodyAttribute(TimelineAttribute::TypeBody, description.left(128).toUtf8());
        item.appendAttribute(bodyAttribute);
    }

//    TimelineAttribute iconAttribute(TimelineAttribute::TypeTinyIcon, TimelineAttribute::IconIDTelegram);
//    item.appendAttribute(iconAttribute);

    if (!fields.isEmpty()) {
        TimelineAttribute fieldNames(TimelineAttribute::TypeFieldNames, fields.keys());
        item.appendAttribute(fieldNames);

        TimelineAttribute fieldValues(TimelineAttribute::TypeFieldValues, fields.values());
        item.appendAttribute(fieldValues);
    }

    if (recurring) {
        TimelineAttribute guess(TimelineAttribute::TypeRecurring, 0x01);
        item.appendAttribute(guess);
    }

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(TimelineAttribute::TypeTitle, "Dismiss");
    dismissAction.appendAttribute(dismissAttribute);
    item.appendAction(dismissAction);

    m_pebble->blobdb()->insert(BlobDB::BlobDBIdPin, item);
}

void TimelineManager::removeTimelinePin(const QUuid &uuid)
{
    qDebug() << "Removing timeline pin:" << uuid;
    m_pebble->blobdb()->remove(BlobDB::BlobDBId::BlobDBIdPin, uuid);
}

void TimelineManager::insertReminder(const QUuid &uuid, const QUuid &parentId, const QString &title, const QString &subtitle, const QString &body, const QDateTime &remindTime)
{

    TimelineItem item(uuid, TimelineItem::TypeReminder, TimelineItem::FlagSingleEvent, remindTime, 0);
    item.setParentId(parentId);

    TimelineAttribute titleAttribute(TimelineAttribute::TypeTitle, title.toUtf8());
    item.appendAttribute(titleAttribute);

    TimelineAttribute subjectAttribute(TimelineAttribute::TypeSubtitle, subtitle.toUtf8());
    item.appendAttribute(subjectAttribute);

    TimelineAttribute bodyAttribute(TimelineAttribute::TypeBody, body.toUtf8());
    item.appendAttribute(bodyAttribute);

    TimelineAttribute iconAttribute(TimelineAttribute::TypeTinyIcon, TimelineAttribute::IconIDAlarmClock);
    item.appendAttribute(iconAttribute);

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(TimelineAttribute::TypeTitle, "Dismiss");
    dismissAction.appendAttribute(dismissAttribute);
    item.appendAction(dismissAction);

    TimelineAction openPinAction(0, TimelineAction::TypeOpenPin);
    TimelineAttribute openPinAttribute(TimelineAttribute::TypeTitle, "More");
    openPinAction.appendAttribute(openPinAttribute);
    item.appendAction(openPinAction);

    TimelineAction muteAction(0, TimelineAction::TypeGeneric);
    TimelineAttribute muteAttribute(TimelineAttribute::TypeTitle, "Mute");
    muteAction.appendAttribute(muteAttribute);
    item.appendAction(muteAction);

    m_pebble->blobdb()->insert(BlobDB::BlobDBIdReminder, item);

}

void TimelineManager::clearTimeline()
{
    foreach (CalendarEvent entry, m_calendarEntries) {
        entry.removeFromCache(m_timelineStoragePath);
    }
    m_calendarEntries.clear();
    m_pebble->blobdb()->clear(BlobDB::BlobDBIdPin);
}

void TimelineManager::syncCalendar(const QList<CalendarEvent> &events)
{
    qDebug() << "BlobDB: Starting calendar sync for" << events.count() << "entries";
    QList<CalendarEvent> itemsToSync;
    QList<CalendarEvent> itemsToAdd;
    QList<CalendarEvent> itemsToDelete;

    // Filter out invalid items
    foreach (const CalendarEvent &event, events) {
        if (event.startTime().isValid() && (event.endTime().isValid() || event.isAllDay())
                && event.startTime().addDays(2) > QDateTime::currentDateTime()
                && QDateTime::currentDateTime().addDays(5) > event.startTime()) {
            itemsToSync.append(event);
        }
    }

    // Compare events to local ones
    foreach (const CalendarEvent &event, itemsToSync) {
        CalendarEvent syncedEvent = findCalendarEvent(event.id());
        if (!syncedEvent.isValid()) {
            itemsToAdd.append(event);
        } else if (!(syncedEvent == event)) {
            qDebug() << "event " << event.id() << " (" << event.title() << ") has changed!";
            syncedEvent.diff(event);
            itemsToDelete.append(syncedEvent);
            itemsToAdd.append(event);
        }
        else {
            qDebug() << "event " << event.id() << " (" << event.title() << ") hasn't changed!";
        }
    }

    // Find stale local ones
    foreach (const CalendarEvent &event, m_calendarEntries) {
        bool found = false;
        foreach (const CalendarEvent &tmp, events) {
            if (tmp.id() == event.id()) {
                found = true;
                break;
            }
        }
        if (!found) {
            qDebug() << "removing stale timeline entry " << event.id() << ": " << event.title();
            itemsToDelete.append(event);
        }
    }

    foreach (const CalendarEvent &event, itemsToDelete) {
        removeTimelinePin(event.uuid());
        if (!event.reminder().isNull()) removeTimelinePin(event.reminderUuid());
        m_calendarEntries.removeAll(event);
        event.removeFromCache(m_timelineStoragePath);
    }

    qDebug() << "adding" << itemsToAdd.count() << "timeline entries";
    foreach (const CalendarEvent &event, itemsToAdd) {
        QMap<QString, QString> fields;
        if (!event.location().isEmpty()) fields.insert("Location", event.location());
        if (!event.calendar().isEmpty()) fields.insert("Calendar", event.calendar());
        if (!event.comment().isEmpty()) fields.insert("Comments", event.comment());
        if (!event.guests().isEmpty()) fields.insert("Guests", event.guests().join(", "));
        insertTimelinePin(event.uuid(), TimelineItem::LayoutCalendar, event.isAllDay(), event.startTime(), event.endTime(), event.title(), event.description(), fields, event.recurring());
        if (!event.reminder().isNull()) {
            qDebug() << "Inserting reminder " << event.reminder();
            insertReminder(event.reminderUuid(), event.uuid(), event.title(), event.location(), event.description(), event.reminder());
        }
        m_calendarEntries.append(event);
        event.saveToCache(m_timelineStoragePath);
    }
}

CalendarEvent TimelineManager::findCalendarEvent(const QString &id)
{
    foreach (const CalendarEvent &entry, m_calendarEntries) {
        if (entry.id() == id) {
            return entry;
        }
    }
    return CalendarEvent();
}


