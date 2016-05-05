#include "timelinemanager.h"
#include "blobdb.h"

#include "watchdatareader.h"
#include "watchdatawriter.h"


#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
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
    if(!QFile::exists(m_timelineStoragePath+"/../layouts.json.auto"))
        QFile::copy(QString(SHARED_DATA_PATH)+"/layouts.json",m_timelineStoragePath+"/../layouts.json.auto");
    reloadLayouts();
}

void TimelineManager::reloadLayouts() {
    QFile lf(m_timelineStoragePath + "/../layouts.json.auto");
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
    for(QVariantMap::const_iterator it=attributes.begin();it!=attributes.end();it++) {
        Attr a;
        a.id = it.value().toMap().value("id").toInt();
        a.max = it.value().toMap().value("max_length").toInt();
        a.type = it.value().toMap().value("type").toString();
        a.note = it.value().toMap().value("note").toString();
        if(it.value().toMap().contains("enum")) {
            QVariantMap enums = it.value().toMap().value("enum").toMap();
            for(QVariantMap::const_iterator et=enums.begin(); et != enums.end(); et++) {
                a.enums.insert(et.key(),(quint8)et.value().toUInt());
            }
        }
        m_attributes.insert(it.key(),a);
    }
    qDebug() << "Added" << m_attributes.size() << "attributes";
    m_resources.clear();
    for(QVariantMap::const_iterator it=resources.begin();it!=resources.end();it++) {
        m_resources.insert(it.key(),it.value().toUInt());
    }
    qDebug() << "Added" << m_resources.size() << "resources";
    m_layouts.clear();
    for(QVariantMap::const_iterator it=layouts.begin(); it!=layouts.end();it++) {
        m_layouts.insert(it.key(),it.value().toUInt());
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
quint32 TimelineManager::getRes(const QString &key) const
{
    return (m_resources.value(key) | 0x80000000);
}
// This one is needed only for migration, can be removed together with color enums from timelineitem.h
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
// This is required to translate colors in pins. Not sure if pebble color names are covered completely
// by W3C defined names but such translation should be bit-correct rather than compressed approximation
QHash<QString,quint8> pebbleCol{
    {"black", 0b11000000},
    {"oxfordblue", 0b11000001},
    {"dukeblue", 0b11000010},
    {"blue", 0b11000011},
    {"darkgreen", 0b11000100},
    {"midnightgreen", 0b11000101},
    {"cobaltblue", 0b11000110},
    {"bluemoon", 0b11000111},
    {"islamicgreen", 0b11001000},
    {"jaegergreen", 0b11001001},
    {"tiffanyblue", 0b11001010},
    {"vividcerulean", 0b11001011},
    {"green", 0b11001100},
    {"malachite", 0b11001101},
    {"mediumspringgreen", 0b11001110},
    {"cyan", 0b11001111},
    {"bulgarianrose", 0b11010000},
    {"imperialpurple", 0b11010001},
    {"indigo", 0b11010010},
    {"electricultramarine", 0b11010011},
    {"armygreen", 0b11010100},
    {"darkgray", 0b11010101},
    {"liberty", 0b11010110},
    {"verylightblue", 0b11010111},
    {"kellygreen", 0b11011000},
    {"maygreen", 0b11011001},
    {"cadetblue", 0b11011010},
    {"pictonblue", 0b11011011},
    {"brightgreen", 0b11011100},
    {"screamingreen", 0b11011101},
    {"mediumaquamarine", 0b11011110},
    {"electricblue", 0b11011111},
    {"darkcandyapplered", 0b11100000},
    {"jazzberryjam", 0b11100001},
    {"purple", 0b11100010},
    {"vividviolet", 0b11100011},
    {"windsortan", 0b11100100},
    {"rosevale", 0b11100101},
    {"purpureus", 0b11100110},
    {"lavenderindigo", 0b11100111},
    {"limerick", 0b11101000},
    {"brass", 0b11101001},
    {"lightgray", 0b11101010},
    {"babyblueeyes", 0b11101011},
    {"springbud", 0b11101100},
    {"inchworm", 0b11101101},
    {"mintgreen", 0b11101110},
    {"celeste", 0b11101111},
    {"red", 0b11110000},
    {"folly", 0b11110001},
    {"fashionmagenta", 0b11110010},
    {"magenta", 0b11110011},
    {"orange", 0b11110100},
    {"sunsetorange", 0b11110101},
    {"brilliantrose", 0b11110110},
    {"shockingpink", 0b11110111},
    {"chromeyellow", 0b11111000},
    {"rajah", 0b11111001},
    {"melon", 0b11111010},
    {"richbrilliantlavender", 0b11111011},
    {"yellow", 0b11111100},
    {"icterine", 0b11111101},
    {"pastelyellow", 0b11111110},
    {"white", 0b11111111}
};
TimelineItem & TimelineManager::parseLayout(TimelineItem &timelineItem, const QJsonObject &layout)
{
    for(QJsonObject::const_iterator it=layout.begin(); it != layout.end(); it++) {
        if(it.key() == "type") {
            if(getLayout(it.value().toString())>0)
                timelineItem.setLayout(getLayout(it.value().toString()));
            else
                qWarning() << "Cannot parse layout type, ignoring" << it.value().toString();
            continue;
        }
        Attr attr = getAttr(it.key());
        if(attr.id==0) {
            qWarning() << "Non-existent attribute" << it.key() << it.value().toString();
            continue;
        }
        TimelineAttribute attribute(attr.id,QByteArray());
        if(attr.type == "string-string") {
            attribute.setContent(it.value().toString().remove(QRegExp("<[^>]*>")).left((attr.max ? attr.max : 64)-1));
        } else if(attr.type == "uri-resource_id") {
            if(getRes(it.value().toString())==0) {
                qWarning() << "Non-existing Resource URI, ignoring" << it.key() << it.value().toString();
                continue;
            }
            attribute.setContent(getRes(it.value().toString()));
        } else if(attr.type == "string_array-string_array") {
            attribute.setContent(it.value().toVariant().toStringList());
        } else if(attr.type == "isodate-unixtime") {
            attribute.setContent(it.value().toVariant().toDateTime().toUTC().toTime_t());
        } else if(attr.type == "color-uint8") {
            QString col = it.value().toString();
            quint8 rgba8 = pebbleCol.value(col);
            if(rgba8 == 0 && col.at(0) != '#') {
                // last attempt - to use QT color names
                QColor qc(col);
                if(qc.isValid()) {
                    col=qc.name(); // should give #RRGGBB formated color string which is parsed down below
                } else {
                    qWarning() << "Cannot parse color definition, ignoring:" << it.key() << col << rgba8 << pebbleCol.contains(col);
                    continue;
                }
            }
            if(rgba8 == 0) { // Cannot be 0 - black is 192(opaque alpha). Parse #RRGGBB color string compressing to rgba8 color space.
                rgba8 = 192 | (((quint8)col.mid(1,2).toInt(0,16)) >> 6) << 4 | (((quint8)col.mid(3,2).toInt(0,16)) >> 6) << 2 | (((quint8)col.mid(5,2).toInt(0,16)) >> 6);
            }
            qDebug() << "Evaluated color to" << rgba8;
            attribute.setContent(rgba8);
        } else if(attr.type == "enum-uint8") {
            if(!attr.enums.contains(it.value().toString())) {
                qWarning() << "Cannot find enum value, ignoring:" << it.key() << it.value().toString();
                continue;
            }
            attribute.setContent(attr.enums.value(it.value().toString()));
        } else if(attr.type == "number-uint32") {
            attribute.setContent((quint32)it.value().toVariant().toUInt());
        } else if(attr.type == "number-int32") {
            attribute.setContent((qint32)it.value().toInt());
        } else if(attr.type == "number-uint16") {
            attribute.setContent((quint16)it.value().toInt());
        } else if(attr.type == "number-int16") {
            attribute.setContent((qint16)it.value().toInt());
        } else if(attr.type == "number-uint8") {
            attribute.setContent((quint8)it.value().toInt());
        } else if(attr.type == "number-int8") {
            attribute.setContent((qint8)it.value().toInt());
        }
        timelineItem.appendAttribute(attribute);
    }
    return timelineItem;
}
enum ActionID {
    ActionDismiss,
    ActionMute,
    ActionOpen,
    ActionPinDefined=10
};

TimelineItem & TimelineManager::createActions(TimelineItem &timelineItem, const QJsonObject &pinMap)
{
    // whether we need to handle standard actions ourselves or let pebble care about them
    bool customHandler = (pinMap.value("type").toString()!="pin");
    if(timelineItem.type() == TimelineItem::TypeNotification || timelineItem.type() == TimelineItem::TypeReminder) {
        // TypeDismiss is not relayed back so use TypeGeneric for customHandler
        TimelineAction dismissAction(ActionDismiss, (customHandler ? TimelineAction::TypeGeneric : TimelineAction::TypeDismiss));
        TimelineAttribute dismissAttribute(getAttr("title").id, gettext("Dismiss"));
        dismissAction.appendAttribute(dismissAttribute);
        timelineItem.appendAction(dismissAction);
    }
    if(pinMap.contains("actions")) {
        qDebug() << "TODO: Add custom actions with id advanced to 10";
    }
    if(timelineItem.type() == TimelineItem::TypeNotification || timelineItem.type() == TimelineItem::TypeReminder) {
        if(!customHandler) {
            TimelineAction actAction(ActionOpen, TimelineAction::TypeOpenPin);
            TimelineAttribute actActionAttribute(getAttr("title").id, gettext("More"));
            actAction.appendAttribute(actActionAttribute);
            timelineItem.appendAction(actAction);

            TimelineAction muteAction(ActionMute, TimelineAction::TypeGeneric);
            TimelineAttribute muteActionAttribute(getAttr("title").id, gettext("Mute"));
            muteAction.appendAttribute(muteActionAttribute);
            timelineItem.appendAction(muteAction);
        } else {
            // For platform notifications let platform handle them as it pleases
            if (pinMap.value("createNotification").toObject().value("layout").toObject().contains("sender")) {
                TimelineAction muteAction(ActionMute, TimelineAction::TypeGeneric);
                TimelineAttribute muteActionAttribute(getAttr("title").id,
                    QString::fromUtf8(gettext("Mute %1")).arg(
                        pinMap.value("createNotification").toObject().value("layout").toObject().value("sender").toString()
                    ).toUtf8()
                );
                muteAction.appendAttribute(muteActionAttribute);
                timelineItem.appendAction(muteAction);
            }
            if (pinMap.contains("sourceAction")) {
                TimelineAction actAction(ActionOpen, TimelineAction::TypeGeneric);
                TimelineAttribute actActionAttribute(getAttr("title").id, gettext("Open on phone"));
                actAction.appendAttribute(actActionAttribute);
                timelineItem.appendAction(actAction);
            }
        }
    }
    return timelineItem;
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

    qDebug() << "Action invoked" << actionId << actionType << notificationId << param << m_notificationSources.value(notificationId);

    BlobDB::Status status = BlobDB::StatusError;
    QList<TimelineAttribute> attributes;

    // Any successfull action is single - you cannot invoke another. So if we succeed - we can remove stored mappings.
    QStringList source = m_notificationSources.value(notificationId);
    QString sourceId = source.first();
    QString actionToken;
    if(source.count()>1)
        actionToken = source.at(1);
    if (sourceId.isEmpty()) {
        status = BlobDB::StatusError;
    } else {
        switch (actionId) {
        case ActionDismiss: { // Dismiss
            TimelineAttribute textAttribute(getAttr("subtitle").id, "Dismissed!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_DISMISSED"));
            attributes.append(iconAttribute);
            emit removeNotification(notificationId);
            status = BlobDB::StatusSuccess;
            m_notificationSources.remove(notificationId);
            break;
        }
        case ActionMute: { // Mute source
            TimelineAttribute textAttribute(getAttr("subtitle").id, "Muted!");
            attributes.append(textAttribute);
            TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_MUTE"));
            attributes.append(iconAttribute);
            emit muteSource(sourceId);
            status = BlobDB::StatusSuccess;
            m_notificationSources.remove(notificationId);
            emit removeNotification(notificationId);
            break;
        }
        case ActionOpen: { // Open on phone
            if(!actionToken.isEmpty()) {
                TimelineAttribute textAttribute(getAttr("subtitle").id, "Opened!");
                attributes.append(textAttribute);
                TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_SENT"));
                attributes.append(iconAttribute);
                qDebug() << "opening" << actionToken;
                emit actionTriggered(notificationId, actionToken);
                status = BlobDB::StatusSuccess;
                m_notificationSources.remove(notificationId);
                // Platform integration removes own mapping anyway, no need to invoke removal
            }
            break;
        }
        default: { // Dunno lol
            if(actionId >= ActionPinDefined) {
                TimelineAttribute textAttribute(getAttr("subtitle").id, "Relayed");
                attributes.append(textAttribute);
                TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_SENT"));
                attributes.append(iconAttribute);
                qDebug() << "executing pin action" << (actionId-ActionPinDefined);
                emit actionRequested(notificationId, (actionId-ActionPinDefined), param);
                status = BlobDB::StatusSuccess;
                m_notificationSources.remove(notificationId);
            } else {
                TimelineAttribute textAttribute(getAttr("subtitle").id, "Action failed!");
                attributes.append(textAttribute);
                TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_FAILED"));
                attributes.append(iconAttribute);
                status = BlobDB::StatusError;
            }
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

void TimelineManager::sendNotification(const QJsonObject &pinMap)
{
    QUuid itemUuid(pinMap.value("guid").toString());
    if(itemUuid.isNull() || !pinMap.contains("createNotification")) {
        qWarning() << "Empty UUID - must be be broken map, ignoring notification";
        return;
    }
    qDebug() << "inserting notification into blobdb:" << itemUuid.toString();
    TimelineItem timelineItem(itemUuid, TimelineItem::TypeNotification);
    timelineItem.setFlags(TimelineItem::FlagSingleEvent);
    timelineItem.setParentId(QUuid(pinMap.value("parent").toString()));

    timelineItem = parseLayout(timelineItem,pinMap.value("createNotification").toObject().value("layout").toObject());
    timelineItem = createActions(timelineItem, pinMap);

    m_pebble->blobdb()->insert(BlobDB::BlobDBIdNotification, timelineItem);
    QStringList source(pinMap.value("kind").toString());
    if (pinMap.contains("sourceAction"))
        source.append(pinMap.value("sourceAction").toString());
    m_notificationSources.insert(itemUuid, source);
}
void TimelineManager::insertTimelinePin(const QJsonDocument &json)
{
    QJsonObject obj = json.object();
    // temporary measure - for the PoC phase
    obj.insert("kind",obj.value("dataSource").toString().split(":").first());
    obj.insert("parent",obj.value("dataSource").toString().split(":").last());
    qDebug() << "Incoming pin:" << QJsonDocument(obj).toJson();
    if(obj.contains("type") && obj.value("type").toString() == "notification") {
        sendNotification(obj);
        return;
    }
    qDebug() << "Generic Pin insert not implemented yet";
}
// Legacy API follows. It's deprecated and will be removed soon.
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

    TimelineAttribute colorAttribute(getAttr("backgroundColor").id,
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
    QStringList source(notification.sourceId());
    if(!notification.actToken().isEmpty())
        source.append(notification.actToken());
    m_notificationSources.insert(itemUuid, source);
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
        TimelineAttribute guess(getAttr("displayRecurring").id, (quint8)0x01);
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

