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
    QFile lf(QString(SHARED_DATA_PATH)+QString("/layouts.json"));
    lf.open(QFile::ReadOnly);
    qDebug() << "Loading layouts file from" << lf.fileName() << lf.errorString();
    QJsonParseError jpe;
    QJsonDocument lj = QJsonDocument::fromJson(lf.readAll(),&jpe);
    lf.close();
    qDebug() << "Parsed with results" << jpe.errorString();
    QVariantMap lm = lj.toVariant().toMap();
    QVariantMap attributes = lm.value("attributes").toMap();
    QVariantMap resources = lm.value("resources").toMap();
    QVariantMap layouts = lm.value("layouts").toMap();
    m_attributes.clear();
    foreach(const QString &key,attributes.keys()) {
        Attr a;
        a.id = attributes.value(key).toMap().value("id").toInt();
        a.max = attributes.value(key).toMap().value("max_length").toInt();
        a.type = attributes.value(key).toMap().value("type").toString();
        a.note = attributes.value(key).toMap().value("note").toString();
        m_attributes.insert(key,a);
    }
    qDebug() << "Added" << m_attributes.size() << "attributes";
    m_resources.clear();
    foreach(const QString &key, resources.keys()) {
        m_resources.insert(key,resources.value(key).toInt());
    }
    qDebug() << "Added" << m_resources.size() << "resources";
    m_layouts.clear();
    foreach(const QString &key, layouts.keys()) {
        m_layouts.insert(key,layouts.value(key).toInt());
    }
    qDebug() << "Added" << m_layouts.size() << "layout types";
}

Attr TimelineManager::getAttr(const QString &key) const
{
    return m_attributes.value(key);
}
quint8 TimelineManager::getLayout(const QString &key) const
{
    return m_layouts.value(key);
}
TimelineAttribute::ResID TimelineManager::getRes(const QString &key) const
{
    return m_resources.value(key);
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
            TimelineAttribute textAttribute(getAttr("subtitle").id, "Dismissed!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_DISMISSED"));
            attributes.append(iconAttribute);
            emit removeNotification(notification.uuid());
            status = BlobDB::StatusSuccess;
            break;
        }
        case 1: { // Mute source
            TimelineAttribute textAttribute(getAttr("subtitle").id, "Muted!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_MUTE"));
            attributes.append(iconAttribute);
            emit muteSource(sourceId);
            status = BlobDB::StatusSuccess;
            break;
        }
        case 2: { // Open on phone
            TimelineAttribute textAttribute(getAttr("subtitle").id, "Opened!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_SENT"));
            attributes.append(iconAttribute);
            qDebug() << "opening" << notification.actToken();
            emit actionTriggered(notification.uuid(), notification.actToken());
            status = BlobDB::StatusSuccess;
            break;
        }
        default: { // Dunno lol
            TimelineAttribute textAttribute(getAttr("subtitle").id, "Action failed!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_FAILED"));
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
const ResMap TimelineManager::NotificationMap[16]  = {
    {"system://images/NOTIFICATION_GENERIC","",-1},
    {"system://images/GENERIC_EMAIL","e mails",TimelineAttribute::ColorGray},
    {"system://images/GENERIC_SMS","SMS",TimelineAttribute::ColorGray2},
    {"system://images/NOTIFICATION_FACEBOOK","Facebook",TimelineAttribute::ColorBlue},
    {"system://images/NOTIFICATION_TWITTER","Twitter",TimelineAttribute::ColorBlue2},
    {"system://images/NOTIFICATION_TELEGRAM","Telegram",TimelineAttribute::ColorLightBlue},
    {"system://images/NOTIFICATION_WHATSAPP","WhatsApp",TimelineAttribute::ColorGreen},
    {"system://images/NOTIFICATION_GOOGLE_HANGOUTS","Google Hangouts",TimelineAttribute::ColorGreen},
    {"system://images/NOTIFICATION_GMAIL","GMail",-1},
    {"system://images/TIMELINE_WEATHER","Weather Notifications",-1},
    {"system://images/MUSIC_EVENT","",-1},
    {"system://images/TIMELINE_MISSED_CALL","call notifications",-1},
    {"system://images/ALARM_CLOCK","",-1},
    {"system://images/NOTIFICATION_REMINDER","reminders",-1},
    {"system://images/NOTIFICATION_FLAG","",-1}
};
void TimelineManager::sendNotification(const Notification &notification)
{
    qDebug() << "inserting notification into blobdb:" << notification.type();
    ResMap notice = TimelineManager::NotificationMap[notification.type()];

    QUuid itemUuid = notification.uuid();
    TimelineItem timelineItem(itemUuid, TimelineItem::TypeNotification);
    timelineItem.setFlags(TimelineItem::FlagSingleEvent);

    timelineItem.setParentId(QUuid("ed429c16-f674-4220-95da-454f303f15e2"));

    TimelineAttribute titleAttribute(getAttr("title").id, notification.sender().remove(QRegExp("<[^>]*>")).left(64).toUtf8());
    timelineItem.appendAttribute(titleAttribute);

    TimelineAttribute subjectAttribute(getAttr("subtitle").id, notification.subject().remove(QRegExp("<[^>]*>")).left(64).toUtf8());
    timelineItem.appendAttribute(subjectAttribute);

    TimelineAttribute bodyAttribute(getAttr("body").id, notification.body().remove(QRegExp("<[^>]*>")).toUtf8());
    timelineItem.appendAttribute(bodyAttribute);

    TimelineAttribute tinyIconAttribute(getAttr("tinyIcon").id, getRes(notice.icon));
    timelineItem.appendAttribute(tinyIconAttribute);

    TimelineAttribute smallIconAttribute(getAttr("smallIcon").id, getRes(notice.icon));
    timelineItem.appendAttribute(smallIconAttribute);

    TimelineAttribute largeIconAttribute(getAttr("largeIcon").id, getRes(notice.icon));
    timelineItem.appendAttribute(largeIconAttribute);

    TimelineAttribute iconAttribute(getAttr("tinyIcon").id, getRes(notice.icon));
    timelineItem.appendAttribute(iconAttribute);

    TimelineAttribute colorAttribute(getAttr("primaryColor").id,
            (notice.color>=0 ? (TimelineAttribute::Color)notice.color : TimelineAttribute::ColorRed));
    timelineItem.appendAttribute(colorAttribute);

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(getAttr("title").id, gettext("Dismiss"));
    dismissAction.appendAttribute(dismissAttribute);
    timelineItem.appendAction(dismissAction);

    if (!notice.mute.isEmpty()) {
        TimelineAction muteAction(1, TimelineAction::TypeGeneric);

        TimelineAttribute muteActionAttribute(getAttr("title").id, QString::fromUtf8(gettext("Mute %1")).arg(notice.mute).toUtf8());
        muteAction.appendAttribute(muteActionAttribute);
        timelineItem.appendAction(muteAction);
    }

    if (!notification.actToken().isEmpty()) {
        TimelineAction actAction(2, TimelineAction::TypeGeneric);
        TimelineAttribute actActionAttribute(getAttr("title").id, gettext("Open on phone"));
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

    TimelineAttribute titleAttribute(getAttr("title").id, title.toUtf8());
    item.appendAttribute(titleAttribute);

    if (!description.isEmpty()) {
        Attr a = getAttr("body");
        TimelineAttribute bodyAttribute(a.id, description.left(a.max).toUtf8());
        item.appendAttribute(bodyAttribute);
    }

//    TimelineAttribute iconAttribute(TimelineAttribute::TypeTinyIcon, TimelineAttribute::IconIDTelegram);
//    item.appendAttribute(iconAttribute);

    if (!fields.isEmpty()) {
        TimelineAttribute fieldNames(getAttr("headings").id, fields.keys());
        item.appendAttribute(fieldNames);

        TimelineAttribute fieldValues(getAttr("paragraphs").id, fields.values());
        item.appendAttribute(fieldValues);
    }

    if (recurring) {
        TimelineAttribute guess(0x1f, (quint8)0x01);
        item.appendAttribute(guess);
    }

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(getAttr("title").id, "Dismiss");
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

    TimelineAttribute titleAttribute(getAttr("title").id, title.toUtf8());
    item.appendAttribute(titleAttribute);

    TimelineAttribute subjectAttribute(getAttr("subtitle").id, subtitle.toUtf8());
    item.appendAttribute(subjectAttribute);

    TimelineAttribute bodyAttribute(getAttr("body").id, body.toUtf8());
    item.appendAttribute(bodyAttribute);

    TimelineAttribute iconAttribute(getAttr("tinyIcon").id, getRes("system://images/ALARM_CLOCK"));
    item.appendAttribute(iconAttribute);

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(getAttr("title").id, "Dismiss");
    dismissAction.appendAttribute(dismissAttribute);
    item.appendAction(dismissAction);

    TimelineAction openPinAction(0, TimelineAction::TypeOpenPin);
    TimelineAttribute openPinAttribute(getAttr("title").id, "More");
    openPinAction.appendAttribute(openPinAttribute);
    item.appendAction(openPinAction);

    TimelineAction muteAction(0, TimelineAction::TypeGeneric);
    TimelineAttribute muteAttribute(getAttr("title").id, "Mute");
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

