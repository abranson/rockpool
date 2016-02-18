#include "blobdb.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

#include <QDebug>
#include <QOrganizerRecurrenceRule>
#include <QDir>
#include <QSettings>

BlobDB::BlobDB(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointBlobDB, this, "blobCommandReply");
    m_connection->registerEndpointHandler(WatchConnection::EndpointActionHandler, this, "actionInvoked");

    connect(m_connection, &WatchConnection::watchConnected, [this]() {
        if (m_currentCommand) {
            delete m_currentCommand;
            m_currentCommand = nullptr;
        }
    });

    m_blobDBStoragePath = m_pebble->storagePath() + "/blobdb/";
    QDir dir(m_blobDBStoragePath);
    if (!dir.exists() && !dir.mkpath(m_blobDBStoragePath)) {
        qWarning() << "Error creating blobdb storage dir.";
        return;
    }
    dir.setNameFilters({"calendarevent-*"});
    foreach (const QFileInfo &fi, dir.entryInfoList()) {
        CalendarEvent event;
        event.loadFromCache(m_blobDBStoragePath, fi.fileName().right(QUuid().toString().length()));

        m_calendarEntries.append(event);
    }
}

void BlobDB::insertNotification(const Notification &notification)
{
    TimelineAttribute::IconID iconId = TimelineAttribute::IconIDDefaultBell;
    TimelineAttribute::Color color = TimelineAttribute::ColorRed;
    QString muteName;
    switch (notification.type()) {
    case Notification::NotificationTypeAlarm:
        iconId = TimelineAttribute::IconIDAlarm;
        muteName = "Alarms";
        break;
    case Notification::NotificationTypeFacebook:
        iconId = TimelineAttribute::IconIDFacebook;
        color = TimelineAttribute::ColorBlue;
        muteName = "facebook";
        break;
    case Notification::NotificationTypeGMail:
        iconId = TimelineAttribute::IconIDGMail;
        muteName = "GMail";
        break;
    case Notification::NotificationTypeHangout:
        iconId = TimelineAttribute::IconIDHangout;
        color = TimelineAttribute::ColorGreen;
        muteName = "Hangout";
        break;
    case Notification::NotificationTypeMissedCall:
        iconId = TimelineAttribute::IconIDDefaultMissedCall;
        muteName = "call notifications";
        break;
    case Notification::NotificationTypeMusic:
        iconId = TimelineAttribute::IconIDMusic;
        muteName = "music";
        break;
    case Notification::NotificationTypeReminder:
        iconId = TimelineAttribute::IconIDReminder;
        muteName = "reminders";
        break;
    case Notification::NotificationTypeTelegram:
        iconId = TimelineAttribute::IconIDTelegram;
        color = TimelineAttribute::ColorLightBlue;
        muteName = "Telegram";
        break;
    case Notification::NotificationTypeTwitter:
        iconId = TimelineAttribute::IconIDTwitter;
        color = TimelineAttribute::ColorBlue2;
        muteName = "Twitter";
        break;
    case Notification::NotificationTypeWeather:
        iconId = TimelineAttribute::IconIDWeather;
        muteName = "Weather";
        break;
    case Notification::NotificationTypeWhatsApp:
        iconId = TimelineAttribute::IconIDWhatsApp;
        color = TimelineAttribute::ColorGreen;
        muteName = "WhatsApp";
        break;
    case Notification::NotificationTypeSMS:
        muteName = "SMS";
        iconId = TimelineAttribute::IconIDDefaultBell;
        break;
    case Notification::NotificationTypeEmail:
    default:
        muteName = "e mails";
        iconId = TimelineAttribute::IconIDDefaultBell;
        break;
    }

    QUuid itemUuid = QUuid::createUuid();
    TimelineItem timelineItem(itemUuid, TimelineItem::TypeNotification);
    timelineItem.setFlags(TimelineItem::FlagSingleEvent);

    TimelineAttribute titleAttribute(TimelineAttribute::TypeTitle, notification.sender().remove(QRegExp("<[^>]*>")).left(64).toUtf8());
    timelineItem.appendAttribute(titleAttribute);

    TimelineAttribute subjectAttribute(TimelineAttribute::TypeSubtitle, notification.subject().remove(QRegExp("<[^>]*>")).left(64).toUtf8());
    timelineItem.appendAttribute(subjectAttribute);

    TimelineAttribute bodyAttribute(TimelineAttribute::TypeBody, notification.body().remove(QRegExp("<[^>]*>")).toUtf8());
    timelineItem.appendAttribute(bodyAttribute);

    TimelineAttribute iconAttribute(TimelineAttribute::TypeTinyIcon, iconId);
    timelineItem.appendAttribute(iconAttribute);

    TimelineAttribute colorAttribute(TimelineAttribute::TypeColor, color);
    timelineItem.appendAttribute(colorAttribute);

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(TimelineAttribute::TypeTitle, "Dismiss");
    dismissAction.appendAttribute(dismissAttribute);
    timelineItem.appendAction(dismissAction);

    TimelineAction muteAction(1, TimelineAction::TypeGeneric);
    TimelineAttribute muteActionAttribute(TimelineAttribute::TypeTitle, "Mute " + muteName.toUtf8());
    muteAction.appendAttribute(muteActionAttribute);
    timelineItem.appendAction(muteAction);

    if (!notification.actToken().isEmpty()) {
        TimelineAction actAction(2, TimelineAction::TypeGeneric);
        TimelineAttribute actActionAttribute(TimelineAttribute::TypeTitle, "Open on phone");
        actAction.appendAttribute(actActionAttribute);
        timelineItem.appendAction(actAction);
    }

    insert(BlobDB::BlobDBIdNotification, timelineItem);
    m_notificationSources.insert(itemUuid, notification);
}

void BlobDB::insertTimelinePin(const QUuid &uuid, TimelineItem::Layout layout, bool isAllDay, const QDateTime &startTime, const QDateTime &endTime, const QString &title, const QString &desctiption, const QMap<QString, QString> fields, bool recurring)
{
//    TimelineItem item(TimelineItem::TypePin, TimelineItem::FlagSingleEvent, QDateTime::currentDateTime().addMSecs(1000 * 60 * 2), 60);

    qDebug() << "inserting timeline pin:" << title << startTime << endTime;
    int duration = (endTime.toMSecsSinceEpoch() - startTime.toMSecsSinceEpoch()) / 1000 / 60;
    TimelineItem::Flag flag = isAllDay ? TimelineItem::FlagAllDay : TimelineItem::FlagSingleEvent;
    TimelineItem item(uuid, TimelineItem::TypePin, flag, startTime, duration);
    item.setLayout(layout);

    TimelineAttribute titleAttribute(TimelineAttribute::TypeTitle, title.toUtf8());
    item.appendAttribute(titleAttribute);

    if (!desctiption.isEmpty()) {
        TimelineAttribute bodyAttribute(TimelineAttribute::TypeBody, desctiption.left(128).toUtf8());
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

    insert(BlobDB::BlobDBIdPin, item);
}

void BlobDB::removeTimelinePin(const QUuid &uuid)
{
    qDebug() << "Removing timeline pin:" << uuid;
    remove(BlobDBId::BlobDBIdPin, uuid);
}

void BlobDB::insertReminder()
{

    TimelineItem item(TimelineItem::TypeReminder, TimelineItem::FlagSingleEvent, QDateTime::currentDateTime().addMSecs(1000 * 60 * 2), 0);

    TimelineAttribute titleAttribute(TimelineAttribute::TypeTitle, "ReminderTitle");
    item.appendAttribute(titleAttribute);

    TimelineAttribute subjectAttribute(TimelineAttribute::TypeSubtitle, "ReminderSubtitle");
    item.appendAttribute(subjectAttribute);

    TimelineAttribute bodyAttribute(TimelineAttribute::TypeBody, "ReminderBody");
    item.appendAttribute(bodyAttribute);

    QByteArray data;
    data.append(0x07); data.append('\0'); data.append('\0'); data.append(0x80);
    TimelineAttribute guessAttribute(TimelineAttribute::TypeTinyIcon, data);
    item.appendAttribute(guessAttribute);
    qDebug() << "attrib" << guessAttribute.serialize();

    TimelineAction dismissAction(0, TimelineAction::TypeDismiss);
    TimelineAttribute dismissAttribute(TimelineAttribute::TypeTitle, "Dismiss");
    dismissAction.appendAttribute(dismissAttribute);
    item.appendAction(dismissAction);

    insert(BlobDB::BlobDBIdReminder, item);
    //    qDebug() << "adding timeline item" << ddd.toHex();

}

void BlobDB::clearTimeline()
{
    foreach (CalendarEvent entry, m_calendarEntries) {
        entry.removeFromCache(m_blobDBStoragePath);
    }
    m_calendarEntries.clear();
    clear(BlobDB::BlobDBIdPin);
}

void BlobDB::syncCalendar(const QList<CalendarEvent> &events)
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
            qDebug() << "event has changed!";
            itemsToDelete.append(syncedEvent);
            itemsToAdd.append(event);
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
            qDebug() << "removing stale timeline entry";
            itemsToDelete.append(event);
        }
    }

    foreach (const CalendarEvent &event, itemsToDelete) {
        removeTimelinePin(event.uuid());
        m_calendarEntries.removeAll(event);
        event.removeFromCache(m_blobDBStoragePath);
    }

    qDebug() << "adding" << itemsToAdd.count() << "timeline entries";
    foreach (const CalendarEvent &event, itemsToAdd) {
        QMap<QString, QString> fields;
        if (!event.location().isEmpty()) fields.insert("Location", event.location());
        if (!event.calendar().isEmpty()) fields.insert("Calendar", event.calendar());
        if (!event.comment().isEmpty()) fields.insert("Comments", event.comment());
        if (!event.guests().isEmpty()) fields.insert("Guests", event.guests().join(", "));
        insertTimelinePin(event.uuid(), TimelineItem::LayoutCalendar, event.isAllDay(), event.startTime(), event.endTime(), event.title(), event.description(), fields, event.recurring());
        m_calendarEntries.append(event);
        event.saveToCache(m_blobDBStoragePath);
    }
}

void BlobDB::clearApps()
{
    clear(BlobDBId::BlobDBIdApp);
    QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
    s.remove("");
}

void BlobDB::insertAppMetaData(const AppInfo &info)
{
    if (!m_pebble->connected()) {
        qWarning() << "Pebble is not connected. Cannot install app";
        return;
    }

    QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
    if (s.value(info.uuid().toString(), false).toBool()) {
        qWarning() << "App already in DB. Not syncing again";
        return;
    }

    AppMetadata metaData = appInfoToMetadata(info, m_pebble->hardwarePlatform());

    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = BlobDBIdApp;

    cmd->m_key = metaData.uuid().toRfc4122();
    cmd->m_value = metaData.serialize();

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::removeApp(const AppInfo &info)
{
    remove(BlobDBId::BlobDBIdApp, info.uuid());
    QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
    s.remove(info.uuid().toString());
}

void BlobDB::insert(BlobDBId database, const TimelineItem &item)
{
    if (!m_connection->isConnected()) {
        return;
    }
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    cmd->m_key = item.itemId().toRfc4122();
    cmd->m_value = item.serialize();

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::remove(BlobDB::BlobDBId database, const QUuid &uuid)
{
    if (!m_connection->isConnected()) {
        return;
    }
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationDelete;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    cmd->m_key = uuid.toRfc4122();

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::clear(BlobDB::BlobDBId database)
{
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationClear;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::setHealthParams(const HealthParams &healthParams)
{
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = BlobDBIdAppSettings;

    cmd->m_key = "activityPreferences";
    cmd->m_value = healthParams.serialize();

    qDebug() << "Setting health params. Enabled:" << healthParams.enabled() << cmd->serialize().toHex();
    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::setUnits(bool imperial)
{
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = BlobDBIdAppSettings;

    cmd->m_key = "unitsDistance";
    WatchDataWriter writer(&cmd->m_value);
    writer.write<quint8>(imperial ? 0x01 : 0x00);

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::blobCommandReply(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint16 token = reader.readLE<quint16>();
    quint8 status = reader.read<quint8>();
    if (m_currentCommand->m_token != token) {
        qWarning() << "Received reply for unexpected token";
    } else if (status != 0x01) {
        qWarning() << "Blob Command failed:" << status;
    } else { // All is well
        if (m_currentCommand->m_database == BlobDBIdApp && m_currentCommand->m_command == OperationInsert) {
            QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
            QUuid appUuid = QUuid::fromRfc4122(m_currentCommand->m_key);
            s.setValue(appUuid.toString(), true);
            emit appInserted(appUuid);
        }
    }

    if (m_currentCommand && token == m_currentCommand->m_token) {
        delete m_currentCommand;
        m_currentCommand = nullptr;
        sendNext();
    }
}

void BlobDB::actionInvoked(const QByteArray &actionReply)
{
    WatchDataReader reader(actionReply);
    TimelineAction::Type actionType = (TimelineAction::Type)reader.read<quint8>();
    QUuid notificationId = reader.readUuid();
    quint8 actionId = reader.read<quint8>();
    quint8 param = reader.read<quint8>(); // Is this correct? So far I've only seen 0x00 in here

    // Not sure what to do with those yet
    Q_UNUSED(actionType)
    Q_UNUSED(param)

    qDebug() << "Action invoked" << actionId << actionReply.toHex();

    Status status = StatusError;
    QList<TimelineAttribute> attributes;

    Notification notification = m_notificationSources.value(notificationId);
    QString sourceId = notification.sourceId();
    if (sourceId.isEmpty()) {
        status = StatusError;
    } else {
        switch (actionId) {
        case 1: { // Mute source
            TimelineAttribute textAttribute(TimelineAttribute::TypeSubtitle, "Muted!");
            attributes.append(textAttribute);
//            TimelineAttribute iconAttribute(TimelineAttribute::TypeLargeIcon, TimelineAttribute::IconIDTelegram);
//            attributes.append(iconAttribute);
            emit muteSource(sourceId);
            status = StatusSuccess;
            break;
        }
        case 2: { // Open on phone
            TimelineAttribute textAttribute(TimelineAttribute::TypeSubtitle, "Opened!");
            attributes.append(textAttribute);
            qDebug() << "opening" << notification.actToken();
            emit actionTriggered(notification.actToken());
            status = StatusSuccess;
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

void BlobDB::sendActionReply()
{

}

void BlobDB::sendNext()
{
    if (m_currentCommand || m_commandQueue.isEmpty()) {
        return;
    }
    m_currentCommand = m_commandQueue.takeFirst();
    m_connection->writeToPebble(WatchConnection::EndpointBlobDB, m_currentCommand->serialize());
}

quint16 BlobDB::generateToken()
{
    return (qrand() % ((int)pow(2, 16) - 2)) + 1;
}

AppMetadata BlobDB::appInfoToMetadata(const AppInfo &info, HardwarePlatform hardwarePlatform)
{
    QString binaryFile = info.file(AppInfo::FileTypeApplication, hardwarePlatform);
    QFile f(binaryFile);
    if (!f.open(QFile::ReadOnly)) {
        qWarning() << "Error opening app binary";
        return AppMetadata();
    }
    QByteArray data = f.read(512);
    WatchDataReader reader(data);
    qDebug() << "Header:" << reader.readFixedString(8);
    qDebug() << "struct Major version:" << reader.read<quint8>();
    qDebug() << "struct Minor version:" << reader.read<quint8>();
    quint8 sdkVersionMajor = reader.read<quint8>();
    qDebug() << "sdk Major version:" << sdkVersionMajor;
    quint8 sdkVersionMinor = reader.read<quint8>();
    qDebug() << "sdk Minor version:" << sdkVersionMinor;
    quint8 appVersionMajor = reader.read<quint8>();
    qDebug() << "app Major version:" << appVersionMajor;
    quint8 appVersionMinor = reader.read<quint8>();
    qDebug() << "app Minor version:" << appVersionMinor;
    qDebug() << "size:" << reader.readLE<quint16>();
    qDebug() << "offset:" << reader.readLE<quint32>();
    qDebug() << "crc:" << reader.readLE<quint32>();
    QString appName = reader.readFixedString(32);
    qDebug() << "App name:" << appName;
    qDebug() << "Vendor name:" << reader.readFixedString(32);
    quint32 icon = reader.readLE<quint32>();
    qDebug() << "Icon:" << icon;
    qDebug() << "Symbol table address:" << reader.readLE<quint32>();
    quint32 flags = reader.readLE<quint32>();
    qDebug() << "Flags:" << flags;
    qDebug() << "Num relocatable entries:" << reader.readLE<quint32>();

    f.close();
    qDebug() << "app data" << data.toHex();

    AppMetadata metadata;
    metadata.setUuid(info.uuid());
    metadata.setFlags(flags);
    metadata.setAppVersion(appVersionMajor, appVersionMinor);
    metadata.setSDKVersion(sdkVersionMajor, sdkVersionMinor);
    metadata.setAppFaceBgColor(0);
    metadata.setAppFaceTemplateId(0);
    metadata.setAppName(appName);
    metadata.setIcon(icon);
    return metadata;

}

CalendarEvent BlobDB::findCalendarEvent(const QString &id)
{
    foreach (const CalendarEvent &entry, m_calendarEntries) {
        if (entry.id() == id) {
            return entry;
        }
    }
    return CalendarEvent();
}

QByteArray BlobDB::BlobCommand::serialize() const
{
    QByteArray ret;
    ret.append((quint8)m_command);
    ret.append(m_token & 0xFF); ret.append(((m_token >> 8) & 0xFF));
    ret.append((quint8)m_database);

    if (m_command == BlobDB::OperationInsert || m_command == BlobDB::OperationDelete) {
        ret.append(m_key.length() & 0xFF);
        ret.append(m_key);
    }
    if (m_command == BlobDB::OperationInsert) {
        ret.append(m_value.length() & 0xFF); ret.append((m_value.length() >> 8) & 0xFF); // value length
        ret.append(m_value);
    }

    return ret;
}
