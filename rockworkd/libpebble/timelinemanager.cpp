#include "timelinemanager.h"
#include "platforminterface.h"
#include "blobdb.h"

#include "watchdatareader.h"
#include "watchdatawriter.h"

#include <QColor>
#include <QDir>

#include <libintl.h>

QHash<QString,TimelineItem::Type> name2type{
    {"notification",TimelineItem::TypeNotification},
    {"commNotification",TimelineItem::TypeNotification},
    {"genericNotification",TimelineItem::TypeNotification},
    {"pin",TimelineItem::TypePin},
    {"genericPin",TimelineItem::TypePin},
    {"calendarPin",TimelineItem::TypePin},
    {"weatherPin",TimelineItem::TypePin},
    {"sportsPin",TimelineItem::TypePin},
    {"reminder",TimelineItem::TypeReminder},
    {"genericReminder",TimelineItem::TypeReminder}
};
QHash<QString,TimelineAction::Type> name2act{
    {"ancsDismiss",TimelineAction::TypeAncsDismiss},
    {"response",TimelineAction::TypeResponse},
    {"dismiss",TimelineAction::TypeDismiss},
    {"http",TimelineAction::TypeHTTP},
    {"snooze",TimelineAction::TypeSnooze},
    {"openWatchApp",TimelineAction::TypeOpenWatchApp},
    {"empty",TimelineAction::TypeEmpty},
    {"remove",TimelineAction::TypeRemove},
    {"open",TimelineAction::TypeOpenPin},
};
const BlobDB::BlobDBId TimelinePin::item2blob[4] = {BlobDB::BlobDBIdTest,BlobDB::BlobDBIdNotification,BlobDB::BlobDBIdPin,BlobDB::BlobDBIdReminder};
TimelinePin::TimelinePin(const TimelinePin &src):
    m_manager(src.m_manager),
    m_uuid(src.m_uuid),
    m_parent(src.m_parent),
    m_kind(src.m_kind),
    m_type(src.m_type),
    m_created(src.m_created),
    m_updated(src.m_updated),
    m_time(src.m_time),
    m_pin(src.m_pin),
    m_topics(src.m_topics),
    m_rejected(src.m_rejected),
    m_sendable(src.m_sendable),
    m_deleted(src.m_deleted),
    m_sent(src.m_sent)
{
    //buildActions();
    qDebug() << "Pin deep copy - be sure to know what you're doing" << m_uuid;
    m_pending = src.m_pending;
}
TimelinePin::TimelinePin(const QJsonObject &obj, TimelineManager *manager, const QUuid &uuid):
    m_manager(manager),
    m_pin(obj)
{
    initJson();
    if(m_uuid.isNull() && !uuid.isNull())
        m_uuid = uuid;
    if(m_created.isNull())
        m_created = QDateTime::currentDateTimeUtc();
}
void TimelinePin::initJson()
{
    if(m_pin.contains("guid"))
        m_uuid = m_pin.value("guid").toVariant().toUuid();
    m_parent = QUuid(m_pin.value("dataSource").toString().split(":").last());
    m_kind = m_pin.value("dataSource").toString().split(":").first();
    m_created = m_pin.value("createTime").toVariant().toDateTime().toUTC();
    m_updated = m_pin.value("updateTime").toVariant().toDateTime().toUTC();
    m_time = m_pin.value("time").toVariant().toDateTime().toUTC();
    m_topics = m_pin.value("topicKeys").toVariant().toStringList();
    if(m_pin.contains("type"))
        m_type = name2type.value(m_pin.value("type").toString(),TimelineItem::TypeInvalid);
    else if(m_pin.contains("layout") && m_pin.value("layout").toObject().contains("type"))
        m_type = name2type.value(m_pin.value("layout").toObject().value("type").toString(),TimelineItem::TypeInvalid);
}
TimelinePin::TimelinePin(const QString &fileName, TimelineManager *manager):
    m_manager(manager)
{
    QFile f_pin(m_manager->m_timelineStoragePath + "/" + fileName);
    if(!f_pin.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Cannot open pin snapshot at" << f_pin.fileName() << f_pin.errorString();
        return;
    }
    QString create = f_pin.readLine();
    QString update = f_pin.readLine();
    QString flags = f_pin.readLine();
    QJsonParseError jpe;
    QJsonDocument pinDoc = QJsonDocument::fromJson(f_pin.readAll(),&jpe);
    f_pin.close();
    if(jpe.error || pinDoc.object().isEmpty()) {
        qWarning() << "Cannot thaw pin" << fileName << jpe.errorString();
        return;
    }
    m_pin = pinDoc.object();
    initJson();
    //qDebug() << "Restoring pin" << m_type << pinDoc.toJson();
    m_sendable = (flags.at(0) == '1');
    m_sent = (flags.at(1) == '1');
    m_rejected = (flags.at(2) == '1');
    m_deleted = (flags.at(3) == '1');
    if(created().isNull() && !create.isEmpty())
        m_created = QDateTime::fromString(create);
    if(updated().isNull() && !update.isEmpty())
        m_updated = QDateTime::fromString(update);
    if(m_uuid.isNull())
        m_uuid=QUuid(fileName);
}

void TimelinePin::flush() const
{
    QFile f_pin(m_manager->m_timelineStoragePath+"/"+m_uuid.toString().mid(1,36));
    if(!f_pin.open(QFile::ReadWrite | QFile::Text)) {
        qWarning() << "Cannot freeze pin to" << f_pin.fileName() << f_pin.errorString();
        return;
    }
    QTextStream out(&f_pin);
    out << m_created.toString(Qt::ISODate) << endl;
    out << m_updated.toString(Qt::ISODate) << endl;
    out << (m_sendable?"1":"0");
    out << (m_sent?"1":"0");
    out << (m_rejected?"1":"0");
    out << (m_deleted?"1":"0") << endl;
    out << QJsonDocument(m_pin).toJson();
    out.flush();
    f_pin.close();
    m_manager->addPin(*this);
}
void TimelinePin::send() const
{
    flush(); // store persistent state and index (add to manager) the pin
    m_pending = true;       // mark as pending
    m_manager->insert(*this); // insert into blobdb
}

void TimelinePin::remove() const
{
    QList<const TimelinePin*> kids = m_manager->pinKids(m_uuid);
    if(!kids.isEmpty()) {
        foreach(const TimelinePin *kid, kids) {
            kid->remove();
        }
    }
    m_pending = true;
    m_manager->remove(*this);
}
void TimelinePin::erase() const
{
    if(m_sent) return;
    QFile::remove(m_manager->m_timelineStoragePath + "/" + m_uuid.toString().mid(1,36));
    m_manager->removePin(m_uuid);
}

void TimelinePin::updateTopics(const TimelinePin &pin)
{
    // Operate on TimelineManager's storage internals. Nasty thing. But pin-specific.
    if(!topics().isEmpty() || !pin.topics().isEmpty()) {
        m_manager->m_mtx_pinStorage.lock();
        foreach (const QString &topic, topics())
            m_manager->m_idx_subscription[topic].removeAll(guid());
        foreach (const QString &topic, pin.topics())
            m_manager->m_idx_subscription[topic].append(guid());
        m_manager->m_mtx_pinStorage.unlock();
        m_pin.insert("topicKeys",pin.m_pin.value("topicKeys"));
        m_topics = pin.topics();
        m_updated = pin.updated();
    }
}

TimelineItem TimelinePin::toItem() const
{
    // I think flags are depricated, even though still present in the protocol. But let's try it out, not much computation
    TimelineItem::Flag flag = (m_type == TimelineItem::TypeNotification) ? TimelineItem::FlagSingleEvent :
                                                (m_pin.contains("allDay") ? TimelineItem::FlagAllDay : TimelineItem::FlagNone);
    TimelineItem timelineItem(guid(), type(), flag, time(), duration());
    qDebug() << "Itemizing pin" << m_uuid;
    timelineItem.setParentId(m_parent);
    timelineItem = m_manager->parseLayout(timelineItem, layout());
    timelineItem = m_manager->parseActions(timelineItem, getActions());
    return timelineItem;
}

TimelinePin::PtrList TimelinePin::kids(TimelineItem::Type type) const
{
    TimelinePin::PtrList ret = m_manager->pinKids(m_uuid);
    for(TimelinePin::PtrList::iterator it=ret.begin();it!=ret.end();it++)
        if((*it)->type()!=type)
            ret.erase(it);
    return ret;
}

const TimelinePin TimelinePin::makeNotification(const TimelinePin *old) const
{
    QString key;
    QJsonValue time;
    if(old!=nullptr) { // Existing pin - update if already sent
        TimelinePin::PtrList kids = old->kids();
        if(m_pin.contains("updateNotification") && !kids.isEmpty() && kids.first()->sent()) {
            qDebug() << "Update notification" << kids.first()->guid() << "for existing pin" << m_uuid;
            key = "updateNotification";
        } else if(m_pin.contains("createNotification") && (kids.isEmpty() || !kids.first()->sent())) {
            qDebug() << "Create notification for existing pin: no notifications sent yet" << m_uuid;
            key = "createNotification";
        }
        if(!key.isEmpty())
            time = m_pin.value(key).toObject().contains("time") ? m_pin.value(key).toObject().value("time") : m_pin.value("createTime");
    } else { // New pin - createNotification
        if(m_pin.contains("createNotification")) {
            qDebug() << "Create new notification for the new pin" << m_uuid;
            key = "createNotification";
            time = m_pin.contains("updateNotification") && m_pin.value("updateNotification").toObject().contains("time") ?
                        m_pin.value("updateNotification").toObject().value("time") : m_pin.value("createTime");
        }
    }
    if(!key.isEmpty()) {
        // Ignore notification more than an hour old
        if(time.toVariant().toDateTime().secsTo(QDateTime::currentDateTimeUtc().addSecs(m_manager->m_event_fadeout))<0) {
            QJsonObject n_pin=m_pin.value(key).toObject();
            n_pin.insert("dataSource",QString("%1:%2").arg(m_pin.value("id").toString(),m_parent.toString().mid(1,36)));
            if(created().isValid())
                n_pin.insert("createTime",created().toString(Qt::ISODate));
            if(updated().isValid())
                n_pin.insert("updateTime",updated().toString(Qt::ISODate));
            n_pin.insert("time",time);
            return TimelinePin(n_pin,m_manager,QUuid::createUuid());
        }
    }
    qDebug() << "Cannot build valid notification for the pin" << m_uuid;
    return TimelinePin();
}

const QList<TimelinePin> TimelinePin::makeReminders() const
{
    QList<TimelinePin> reminders;
    for(int i = 0; i < qMin(m_pin.value("reminders").toArray().size(),3);i++) {
        QJsonObject obj=m_pin.value("reminders").toArray().at(i).toObject();
        QDateTime at = obj.value("time").toVariant().toDateTime().toUTC();
        if(at > QDateTime::currentDateTimeUtc().addSecs(-15*60))
            reminders.append(TimelinePin(obj,m_manager,QUuid::createUuid()));
        else
            qDebug() << "Reminder" << obj.value("time").toString() << "has expired";
    }
    return reminders;
}
void TimelinePin::buildActions() const
{
    if(m_pin.contains("actions")) {
        QJsonArray acts = m_pin.value("actions").toArray();
        for(int i=0;i<acts.size();i++) {
            qDebug() << "Adding action" << acts[i].toObject().value("type").toString() << acts[i].toObject().value("title").toString();
            m_actions.append(acts[i]);
        }
    }
    if(!m_actions.first().toObject().value("type").toString().startsWith("dismiss") &&
            (m_type == TimelineItem::TypeNotification || m_type == TimelineItem::TypeReminder)) {
        QJsonObject action;
        // TypeDismiss is not relayed back so use TypeGeneric if you need to handle it
        action.insert("type",QString("dismiss"));
        action.insert("title",QString(gettext("Dismiss")));
        m_actions.prepend(action);
    }
    if(m_actions.last().toObject().value("type").toString() != "mute") {
        QJsonObject actOpen,actMute;
        if(m_type == TimelineItem::TypePin) {
            actOpen.insert("type",QString("remove"));
            actOpen.insert("title",QString("Remove"));
            m_actions.append(actOpen);
        } else {
            actOpen.insert("type",QString("open"));
            actOpen.insert("title",QString("Open"));
            m_actions.append(actOpen);
        }
        actMute.insert("type",QString("mute"));
        actMute.insert("title",QString("Mute"));
        m_actions.append(actMute);
    }
}

const QJsonArray & TimelinePin::getActions() const
{
    if(!m_actions.isEmpty())
        return m_actions;
    buildActions();
    return m_actions;
}
QList<TimelineAttribute> TimelinePin::handleAction(TimelineAction::Type atype, quint8 id, const QJsonObject &param) const
{
    if(m_actions.isEmpty()) {
        qWarning() << "Action for re-serialized event - platform event must already be missing";
        buildActions();
    }
    Q_UNUSED(atype)
    QJsonObject action = m_actions.at(id).toObject();
    QString a_type = action.value("type").toString();
    QList<TimelineAttribute> attributes;
    if(a_type == "mute") {
        emit m_manager->muteSource(m_kind);
        attributes.append(m_manager->parseAttribute("largeIcon",QString("system://images/RESULT_MUTED")));
    } else if(a_type.startsWith("dismiss")) {
        remove();
        attributes.append(m_manager->parseAttribute("largeIcon",QString("system://images/RESULT_DISMISSED")));
        attributes.append({m_manager->getAttr("subtitle").id,"Dismissed!"});
    } else if(a_type == "remove" && type() == TimelineItem::TypePin) {
        remove();
        attributes.append(m_manager->parseAttribute("largeIcon",QString("system://images/RESULT_DELETED")));
        attributes.append({m_manager->getAttr("subtitle").id,"Removed!"});
    } else {
        emit m_manager->actionTriggered(m_uuid,a_type, param);
        attributes.append(m_manager->parseAttribute("largeIcon",QString("system://images/RESULT_SENT")));
    }
    if(attributes.count()==1)
        attributes.append({m_manager->getAttr("subtitle").id,"Done!"});
    return attributes;
}
/**
 * @brief TimelineManager::TimelineManager
 * @param pebble
 * @param connection
 *
 * Constructs Timeline Manager for given Pebble instance with given transport connection to pebble
 * It will try to load legacy calendar events from blobdb storage, converting them to timeline pin
 *
 * During construction it loads layouts.json.auto which will be either taken from packaged hard
 * copy or stashed during latest firmware upgrade.
 *
 * Once all pins are loaded - maintenance cycle is scheduled to cleanup/resend/retain pins.
 * Additionally maintenance cycle is enforced on pebble connection - to resend pending pins.
 */
TimelineManager::TimelineManager(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointActionHandler, this, "actionHandler");
    connect(m_pebble->blobdb(), &BlobDB::blobCommandResult, this, &TimelineManager::blobdbAckHandler);
    m_timelineStoragePath = pebble->storagePath() + "timeline";
    // Load firmware layout map
    if(!QFile::exists(m_timelineStoragePath+"/../layouts.json.auto"))
        QFile::copy(QString(SHARED_DATA_PATH)+"/layouts.json",m_timelineStoragePath+"/../layouts.json.auto");
    reloadLayouts();
    // Load persistent pins
    QDir dir=QDir(m_timelineStoragePath);
    if (!dir.exists() && !dir.mkpath(m_timelineStoragePath)) {
        qWarning() << "Error creating timeline storage dir.";
        return;
    } else {
        dir.setNameFilters({"*-*-*-*-*"});
        foreach (const QFileInfo &fi, dir.entryInfoList()) {
            TimelinePin pin(fi.fileName(),this);
            if(pin.isValid())
                addPin(pin);
            else
                qDebug() << "Ignoring broken pin" << fi.fileName();
        }
    }
#ifdef DATA_MIGRATION
    // It's more safe and efficient to resync than migrate. However to reset timeline
    // we need connected pebble, which is not the case in construction time.
    // Better to do it manually then using Reset button in application settings.
    QString calCache = pebble->storagePath() + "blobdb";
    dir=QDir(calCache);
    if(dir.exists()) {
        dir.setNameFilters({"calendarevent-*"});
        foreach (const QFileInfo &fi, dir.entryInfoList()) {
            dir.remove(fi.fileName());
        }
        if(dir.entryInfoList().count()>0) {
            pebble->resetTimeline();
        }
    }
#endif // DATA_MIGRATION
    // Also run maintenance cycle on watch connection - to redeliver notifications and stuff
    connect(connection, &WatchConnection::watchConnected, this, &TimelineManager::doMaintenance, Qt::QueuedConnection);
    startTimer(180000);
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
static const TimelineAttribute att_inval(quint8(0),QByteArray());
TimelineAttribute TimelineManager::parseAttribute(const QString &key, const QJsonValue &val)
{
    Attr attr = getAttr(key);
    if(attr.id==0) {
        qWarning() << "Non-existent attribute" << key << val.toString();
        return att_inval;
    }
    TimelineAttribute attribute(attr.id,QByteArray());
    if(attr.type == "string-string") {
        attribute.setContent(val.toString().remove(QRegExp("<[^>]*>")).toUtf8().left((attr.max ? attr.max : 64)-1));
    } else if(attr.type == "uri-resource_id") {
        if(getRes(val.toString())==0) {
            qWarning() << "Non-existing Resource URI, ignoring" << key << val.toString();
            return att_inval;
        }
        attribute.setContent(getRes(val.toString()));
    } else if(attr.type == "string_array-string_array") {
        attribute.setContent(val.toVariant().toStringList());
    } else if(attr.type == "isodate-unixtime") {
        attribute.setContent(val.toVariant().toDateTime().toUTC().toTime_t());
    } else if(attr.type == "color-uint8") {
        QString col = val.toString();
        quint8 rgba8 = pebbleCol.value(col);
        if(rgba8 == 0 && col.at(0) != '#') {
            // last attempt - to use QT color names
            QColor qc(col);
            if(qc.isValid()) {
                col=qc.name(); // should give #RRGGBB formated color string which is parsed down below
            } else {
                qWarning() << "Cannot parse color definition, ignoring:" << key << col << rgba8 << pebbleCol.contains(col);
                return att_inval;
            }
        }
        if(rgba8 == 0) { // Cannot be 0 - black is 192(opaque alpha). Parse #RRGGBB color string compressing to rgba8 color space.
            rgba8 = 192 | (((quint8)col.mid(1,2).toInt(0,16)) >> 6) << 4 | (((quint8)col.mid(3,2).toInt(0,16)) >> 6) << 2 | (((quint8)col.mid(5,2).toInt(0,16)) >> 6);
        }
        qDebug() << "Evaluated color to" << rgba8;
        attribute.setContent(rgba8);
    } else if(attr.type == "enum-uint8") {
        if(!attr.enums.contains(val.toString())) {
            qWarning() << "Cannot find enum value, ignoring:" << key << val.toString();
            return att_inval;
        }
        attribute.setContent(attr.enums.value(val.toString()));
    } else if(attr.type == "number-uint32") {
        attribute.setContent((quint32)val.toVariant().toUInt());
    } else if(attr.type == "number-int32") {
        attribute.setContent((qint32)val.toInt());
    } else if(attr.type == "number-uint16") {
        attribute.setContent((quint16)val.toInt());
    } else if(attr.type == "number-int16") {
        attribute.setContent((qint16)val.toInt());
    } else if(attr.type == "number-uint8") {
        attribute.setContent((quint8)val.toInt());
    } else if(attr.type == "number-int8") {
        attribute.setContent((qint8)val.toInt());
    }
    return attribute;
}
//QJsonObject &TimelineManager::deserializeAttribute(const TimelineAttribute &attr, QJsonObject &obj)
QJsonObject &TimelineManager::deserializeAttribute(quint8 type, const QByteArray &buf, QJsonObject &obj)
{
    foreach(const QString &key,m_attributes.keys()) {
        Attr a = getAttr(key);
        if(a.id==type) {
            if(a.type == "string_array-string_array") {
                QJsonArray lst;
                foreach(const QByteArray ar,buf.split('\0')) {
                    lst.append(QString(ar));
                }
                obj.insert(key,lst);
            } else if(a.type == "string-string") {
                obj.insert(key,QString(buf));
            } else if(a.type == "number-uint8") {
                obj.insert(key,(quint8)buf.at(0));
            } else {
                qDebug() << "What else?" << a.type;
            }
            return obj;
        }
    }
    qDebug() << "Cannot find attribute of type" << type << "something needs upgrade";
    return obj;
}
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
        TimelineAttribute attribute = parseAttribute(it.key(),it.value());
        if(attribute.type()>0)
            timelineItem.appendAttribute(attribute);
    }
    return timelineItem;
}

TimelineItem & TimelineManager::parseActions(TimelineItem &timelineItem, const QJsonArray &actions)
{
    for(int i=0;i<actions.size();i++) {
        QJsonObject act=actions[i].toObject();
        qDebug() << "Adding action" << act.value("type").toString() << name2act.value(act.value("type").toString(),TimelineAction::TypeGeneric);
        TimelineAction tlAct(i,name2act.value(act.value("type").toString(),TimelineAction::TypeGeneric));
        for(QJsonObject::const_iterator it=act.begin();it!=act.end();it++) {
            if(it.key() == "type")
                continue;
            TimelineAttribute attr=parseAttribute(it.key(),it.value());
            qDebug() << "With attribute" << it.key() << it.value() << attr.type();
            if(attr.type()>0)
                tlAct.appendAttribute(attr);
        }
        timelineItem.appendAction(tlAct);
    }
    return timelineItem;
}
void TimelineManager::actionHandler(const QByteArray &actionReply)
{
    WatchDataReader reader(actionReply);
    TimelineAction::Type actionType = (TimelineAction::Type)reader.read<quint8>();
    QUuid notificationId = reader.readUuid();
    quint8 actionId = reader.read<quint8>();
    quint8 att_num = reader.read<quint8>();
    QJsonObject param;
    qDebug() << "Action invoked" << actionId << actionType << notificationId << att_num;
    for(int i=0;i<att_num;i++) {
        quint8 type = reader.read<quint8>();
        quint16 len = reader.readLE<quint16>();
        QByteArray buf = reader.readBytes(len);
        qDebug() << "Attribute type" << type << "length" << len << buf;
        param=deserializeAttribute(type,buf,param);
    }
    if(!param.isEmpty())
         qDebug() << QJsonDocument(param).toJson();

    BlobDB::Response status = BlobDB::ResponseError;
    QList<TimelineAttribute> attributes;

    const TimelinePin *source = getPin(notificationId);
    if (source==nullptr) {
        status = BlobDB::ResponseError;
        qWarning() << "Action for non-existing pin" << notificationId;
        emit removeNotification(notificationId);
    } else {
        switch (actionType) {
        case TimelineAction::TypeDismiss:
        case TimelineAction::TypeGeneric:
        case TimelineAction::TypeHTTP:
        case TimelineAction::TypeResponse:
            attributes = source->handleAction(actionType,actionId,param);
            break;
        default:
            qDebug() << "We aren't supposed to handle anything else, who's calling? why?";
        }
    }
    if(attributes.isEmpty()) {
        TimelineAttribute textAttribute(getAttr("subtitle").id, "Action failed!");
        attributes.append(textAttribute);
        TimelineAttribute iconAttribute(getAttr("largeIcon").id, getRes("system://images/RESULT_FAILED"));
        attributes.append(iconAttribute);
        status = BlobDB::ResponseError;
    } else {
        status = BlobDB::ResponseSuccess;
        // Once event is actioned - it's stored with no other actions possible. Release platfrorm part.
        emit removeNotification(notificationId);
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

// Storage Ops
void TimelineManager::addPin(const TimelinePin &pin)
{
    m_mtx_pinStorage.lock();
    // We may be re-inserting the pin - eg update. Parent is ok by time & topics may change.
    if(m_pin_idx_guid.contains(pin.guid())) {
        m_pin_idx_time[m_pin_idx_guid.value(pin.guid()).gmtime_t()].removeAll(pin.guid());
        foreach(const QString &topic,m_pin_idx_guid.value(pin.guid()).topics())
            m_idx_subscription[topic].removeAll(pin.guid());
    }
    m_pin_idx_guid.insert(pin.guid(),pin);
    m_pin_idx_parent[pin.parent()].append(pin.guid());
    m_pin_idx_time[pin.gmtime_t()].append(pin.guid());
    foreach(const QString &topic,pin.topics())
        m_idx_subscription[topic].append(pin.guid());
    m_mtx_pinStorage.unlock();
}
void TimelineManager::removePin(const QUuid &guid)
{
    qDebug() << "Removing timeline pin:" << guid.toString();
    if(m_pin_idx_guid.contains(guid)) {
        m_mtx_pinStorage.lock();
        TimelinePin pin=m_pin_idx_guid.take(guid);
        if(m_pin_idx_parent.value(pin.parent()).count()==1)
            m_pin_idx_parent.remove(pin.parent());
        else
            m_pin_idx_parent[pin.parent()].removeAll(guid);
        if(m_pin_idx_time.value(pin.gmtime_t()).count()==1)
            m_pin_idx_time.remove(pin.gmtime_t());
        else
            m_pin_idx_time[pin.gmtime_t()].removeAll(guid);
        foreach(const QString &topic,pin.topics())
            m_idx_subscription[topic].removeAll(guid);
        m_mtx_pinStorage.unlock();
    }
}

bool TimelineManager::pinExists(const QUuid &guid) const
{
    return m_pin_idx_guid.contains(guid);
}

TimelinePin * TimelineManager::getPin(const QUuid &guid)
{
    if(m_pin_idx_guid.contains(guid))
        return &(m_pin_idx_guid[guid]);
    qCritical() << "Requested non-existing pin" << guid;
    return nullptr;
}
const QList<const TimelinePin*> TimelineManager::pinKids(const QUuid &parent)
{
    QList<const TimelinePin*> ret;
    foreach(const QUuid & uuid,m_pin_idx_parent.value(parent)) {
        if(m_pin_idx_guid.contains(uuid))
            ret.append(&(m_pin_idx_guid[uuid]));
        else
            qCritical() << "Pin storage corruption: child" << uuid << "referenced by parent" << parent << "is missing";
    }
    return ret;
}
quint32 TimelineManager::pinCount(const QUuid *parent)
{
    if(parent)
        return m_pin_idx_parent.value(*parent).count();
    else
        return m_pin_idx_guid.count();
}

void TimelineManager::setTimelineWindow(int daysPast, int eventFadeout, int daysFuture)
{
    m_past_days = daysPast;
    m_event_fadeout = eventFadeout;
    m_future_days = daysFuture;
}

void TimelineManager::timerEvent(QTimerEvent *event)
{
    if(event) {
        doMaintenance();
    }
}

void TimelineManager::doMaintenance()
{
    // End is future boundary - now+7. 7 is calendar window, pypkjs uses +4.
    time_t window_end = QDateTime::currentDateTimeUtc().addDays(m_future_days).toTime_t();
    // Start is past boundary - now-2. This is questionable. Pebble keeps up to 72hrs.
    time_t window_start = QDateTime::currentDateTimeUtc().addDays(m_past_days).toTime_t();
    // Notification fadeout - we don't want notifications older than an hour.
    time_t event_horizon = QDateTime::currentDateTimeUtc().addSecs(m_event_fadeout).toTime_t();
    // Delayed removal - to keep iterator consistent
    QList<const TimelinePin*> cleanup;
    qDebug() << "Executing maintenance cycle" << window_start << event_horizon << window_end;
    // Traverse items from most recent to oldest
    QMap<time_t,QList<QUuid>>::iterator it=m_pin_idx_time.end();
    if(!m_pin_idx_time.empty()) do {
        it--;
        //qDebug() << "Iterating timestamp" << it.key() << "with" << it.value().count() << "items";
        if(it.value().isEmpty()) {
            m_mtx_pinStorage.lock();
            m_pin_idx_time.erase(it);
            m_mtx_pinStorage.unlock();
            continue;
        }
        foreach(const QUuid &guid,it.value()) {
            const TimelinePin *pin = getPin(guid);
            if(pin!=nullptr) {
                if(pin->pending()) {
                    qDebug() << "Skipping pending item. If it persists - something is wrong." << pin->guid();
                    continue; // Skip pending
                }
                if(it.key() > window_start && it.key() < window_end) {
                    // Within the window - resend undelivered
                    if(!pin->deleted() && !pin->sent() && !pin->rejected()) {
                        // Except notifications - drop obsolete
                         if(pin->type()==TimelineItem::TypeNotification && it.key() < event_horizon) {
                            qDebug() << "Discarding stale notification" << guid;
                            cleanup.append(pin);
                            emit removeNotification(guid);
                        } else {
                            qDebug() << "Resending unsent pin" << guid;
                            pin->send();
                        }
                    } if(pin->deleted() && pin->type()==TimelineItem::TypeNotification) {
                        qDebug() << "Removing dismissed event" << guid;
                        cleanup.append(pin);
                        // Dismiss should emit removeNotification
                    } if(pin->sent() && !pin->sendable()) {
                        qDebug() << "Pending deleteion for pin, removing" << guid;
                        pin->remove();
                    //} else {
                    //    qDebug() << "Keeping pin" << guid;
                    }
                } else {
                    // Out of the window - clean'em'up
                    if(pin->sent()) {
                        qDebug() << "Revoking obsolete pin" << guid;
                        pin->remove();
                        // will be sent for all pins, but platform should cope with ignoring irrelevant.
                        emit removeNotification(guid);
                    } else {
                        // We don't really care what state it is now, just clean unsent up
                        qDebug() << "Discarding obsolete pin" << guid;
                        cleanup.append(pin);
                    }
                }
            } else {
                qWarning() << "Non-existing pin reference in the timeline" << guid;
                it.value().removeAll(guid);
            }
        }
    } while(it!=m_pin_idx_time.begin());
    qDebug() << "Cleaning up" << cleanup.size() << "discarded pins";
    foreach(const TimelinePin*pin,cleanup)
        pin->erase();
}

// Don't call these directly, pin will call it when needed
void TimelineManager::insert(const TimelinePin &pin)
{
    qDebug() << "inserting TimelinePin into blobdb:" << pin.blobId() << pin.guid().toString();
    m_pebble->blobdb()->insert(pin.blobId(), pin.toItem());
}
void TimelineManager::remove(const TimelinePin &pin)
{
    qDebug() << "removing TimelinePin from blobdb:" << pin.blobId() << pin.guid().toString();
    m_pebble->blobdb()->remove(pin.blobId(), pin.guid());
}

void TimelineManager::clearTimeline(const QUuid &parent)
{
    if(parent.isNull()) {
        wipeTimeline();
    } else {
        foreach (const TimelinePin *pin, pinKids(parent)) {
            pin->remove();
            pin->erase();
        }
        if(pinCount()==0) {
            m_pebble->blobdb()->clear(BlobDB::BlobDBIdPin);
            m_pebble->blobdb()->clear(BlobDB::BlobDBIdReminder);
            m_pebble->blobdb()->clear(BlobDB::BlobDBIdNotification);
        }
    }
}

void TimelineManager::wipeTimeline(const QString &source)
{
    // Let's do simple traversal till we find better way to handle that
    foreach(const TimelinePin &pin,m_pin_idx_guid.values()) {
        if(source.isEmpty()) {
            pin.erase();
        } else if(source == pin.source()) {
            pin.remove();
            pin.erase();
        }
    }
    if(pinCount()==0) {
        m_pebble->blobdb()->clear(BlobDB::BlobDBIdPin);
        m_pebble->blobdb()->clear(BlobDB::BlobDBIdReminder);
        m_pebble->blobdb()->clear(BlobDB::BlobDBIdNotification);
    }
}

void TimelineManager::wipeSubscription(const QString &topic)
{
    foreach (const QUuid &guid, m_idx_subscription.value(topic)) {
        TimelinePin *pin = getPin(guid);
        if(pin) {
            pin->remove();
        }
    }
}

void TimelineManager::blobdbAckHandler(BlobDB::BlobDBId db, BlobDB::Operation cmd, const QUuid &uuid, BlobDB::Status ack)
{
    switch(db) {
    case BlobDB::BlobDBIdPin:
    case BlobDB::BlobDBIdNotification:
    case BlobDB::BlobDBIdReminder:
        break;
    default:
        return;
    }
    TimelinePin *pin = getPin(uuid);
    if(pin==nullptr) {
        qDebug() << "Result for non-existing pin" << uuid << db << cmd << ack;
        return;
    }
    qDebug() << ((ack==BlobDB::StatusSuccess)?"ACK":"NACK") << "for" << ((cmd==BlobDB::OperationInsert)?"insert":"delete") << "of" << pin->guid();
    if(cmd == BlobDB::OperationInsert) {
        switch(ack) {
        case BlobDB::StatusSuccess:
            pin->setSent(true);
            break;
        case BlobDB::StatusFailure:
        case BlobDB::StatusIgnore:
            pin->setRejected(false);
            break;
        default:
            pin->setRejected(true);
        }
        pin->flush();
    } else if (cmd == BlobDB::OperationDelete) {
        switch(ack) {
        case BlobDB::StatusNoSuchKey:
        case BlobDB::StatusSuccess:
            pin->setDeleted(true);
            break;
        default:
            pin->setDeleted(false);
            pin->setSendable(false);
        }
        pin->flush();
    } else if(cmd == BlobDB::OperationClear) {
        if(ack == BlobDB::StatusSuccess) {
            // TODO:
            // drop all pins and indexes
            // request full re-sync
            qDebug() << "Someone cleared the timeline";
            m_pebble->syncCalendar();
        }
    }
}

void TimelineManager::insertTimelinePin(const QJsonObject &json)
{
    QJsonObject obj(json);
    qDebug() << "Incoming pin:" << QJsonDocument(obj).toJson();
    TimelinePin pin(obj,this);
    if(pin.type() == TimelineItem::TypeNotification) {
        // Simple persistence checks for volatile (system) notification. Also do some sanity checks
        if(!pin.guid().isNull() && !pin.layout().isEmpty() && !pin.kind().isEmpty() && !pin.parent().isNull() && !pinExists(pin.guid()))
            pin.send();
        else
            qWarning() << (pin.guid().isNull()?"GUID":"") << (pin.layout().isEmpty()?"Layout":"") << (pin.kind().isEmpty()?"Kind":"") << (pin.parent().isNull()?"Parent":"") << (pinExists(pin.guid())?"Notification pin exists,":"missing from notification,") << "ignoring.";
        return;
    }
    // Below logic is mimicking reference implementation at pypkjs
    TimelinePin *old = getPin(pin.guid());
    if(old!=nullptr) {
        if(old->deleted()) {
            qDebug() << "Pin was deleted, ignoring";
            return;
        }
        if(old->updated() >= pin.updated()) {
            if(old->updated()==pin.updated())
                old->updateTopics(pin);
            qDebug() << "Existing pin, refreshing and skipping";
            return;
        }
        qDebug() << "Update for existing pin" << old->updated() << pin.updated();
        if(!old->reminders().isEmpty()) {
            qDebug() << "Removing old reminders - we'll add 'm later";
            foreach(const TimelinePin* kid, pinKids(old->guid())) {
                if(kid->type()==TimelineItem::TypeReminder)
                    kid->remove();
            }
        }
    }
    // At this stage we are positive to insert the pin.
    // Insert it first so that user could open it from notification or reminder
    qDebug() << "Sending pin" << pin.guid() << pin.time().toString(Qt::ISODate);
    pin.send();
    TimelinePin notice = pin.makeNotification(old);
    if(notice.isValid()) {
        qDebug() << "Sending notification" << notice.guid() << "for pin" << pin.guid();
        notice.send(); // Store, add to index and send to watches
    }
    if(!pin.reminders().isEmpty()) {
        qDebug() << "Sending" << pin.reminders().count() << "reminders for pin" << pin.guid();
        foreach(const TimelinePin &rmd,pin.makeReminders()) {
            qDebug() << rmd.guid() << rmd.time().toString(Qt::ISODate);
            rmd.send();
        }
    }
}
void TimelineManager::removeTimelinePin(const QString &guid)
{
    TimelinePin * pin = getPin(QUuid(guid));
    if(pin!=nullptr)
        pin->remove();
}
