#include "timelineitem.h"

TimelineItem::TimelineItem(TimelineItem::Type type, Flags flags, const QDateTime &timestamp, quint16 duration):
    TimelineItem(QUuid::createUuid(), type, flags, timestamp, duration)
{

}

TimelineItem::TimelineItem(const QUuid &uuid, TimelineItem::Type type, Flags flags, const QDateTime &timestamp, quint16 duration):
    BlobDbItem(),
    m_itemId(uuid),
    m_timestamp(timestamp),
    m_duration(duration),
    m_type(type),
    m_flags(flags)
{

}
TimelineItem::TimelineItem():
    TimelineItem(QUuid(),TimelineItem::TypeInvalid,0)
{

}

QUuid TimelineItem::itemId() const
{
    return m_itemId;
}

QDateTime TimelineItem::ts() const
{
    return m_timestamp;
}

void TimelineItem::setParentId(QUuid parentId) {
    m_parentId = parentId;
}

void TimelineItem::setLayout(quint8 layout)
{
    m_layout = layout;
}

void TimelineItem::setFlags(Flags flags)
{
    m_flags = flags;
}

void TimelineItem::appendAttribute(const TimelineAttribute &attribute)
{
    m_attributes.append(attribute);
}

void TimelineItem::appendAction(const TimelineAction &action)
{
    m_actions.append(action);
}

QList<TimelineAttribute> TimelineItem::attributes() const
{
    return m_attributes;
}

QList<TimelineAction> TimelineItem::actions() const
{
    return m_actions;
}

QByteArray TimelineItem::itemKey() const
{
    return itemId().toRfc4122();
}

QByteArray TimelineItem::serialize() const
{
    QByteArray ret;
    ret.append(m_itemId.toRfc4122());
    ret.append(m_parentId.toRfc4122());
    int ts = m_timestamp.toMSecsSinceEpoch() / 1000;
    ret.append(ts & 0xFF); ret.append((ts >> 8) & 0xFF); ret.append((ts >> 16) & 0xFF); ret.append((ts >> 24) & 0xFF);
    ret.append(m_duration & 0xFF); ret.append(((m_duration >> 8) & 0xFF));
    ret.append((quint8)m_type);
    ret.append(m_flags & 0xFF); ret.append(((m_flags >> 8) & 0xFF));
    ret.append(m_layout);

    QByteArray serializedAttributes;
    foreach (const TimelineAttribute &attribute, m_attributes) {
        serializedAttributes.append(attribute.serialize());
    }

    QByteArray serializedActions;
    foreach (const TimelineAction &action, m_actions) {
        serializedActions.append(action.serialize());
    }
    quint16 dataLength = serializedAttributes.length() + serializedActions.length();
    ret.append(dataLength & 0xFF); ret.append(((dataLength >> 8) & 0xFF));
    ret.append(m_attributes.count());
    ret.append(m_actions.count());
    ret.append(serializedAttributes);
    ret.append(serializedActions);
    return ret;
}
bool TimelineItem::deserialize(const QByteArray &data)
{
    WatchDataReader r(data);
    if(r.checkBad(43)) return false;
    m_itemId = r.readUuid();
    m_parentId = r.readUuid();
    quint32 ts = r.readLE<quint32>();
    m_timestamp = QDateTime::fromTime_t(ts,Qt::UTC);
    m_duration = r.readLE<quint16>();
    m_flags = (TimelineItem::Flag)r.readLE<quint16>();
    m_layout = r.read<quint8>();
    quint16 dataLength = r.readLE<quint16>();
    quint8 attc = r.read<quint8>();
    quint8 actc = r.read<quint8>();
    if(r.checkBad(dataLength)) return false;
    for(int i=0;i<attc;i++) {
        TimelineAttribute a;
        if(a.deserialize(r))
            m_attributes.append(a);
        else
            return false;
    }
    for(int i=0;i<actc;i++) {
        TimelineAction a;
        if(a.deserialize(r))
            m_actions.append(a);
        else {
            return false;
        }
    }
    return true;
}

TimelineAction::TimelineAction(quint8 actionId, TimelineAction::Type type, const QList<TimelineAttribute> &attributes):
    PebblePacket(),
    m_actionId(actionId),
    m_type(type),
    m_attributes(attributes)
{

}

void TimelineAction::appendAttribute(const TimelineAttribute &attribute)
{
    m_attributes.append(attribute);
}

QByteArray TimelineAction::serialize() const
{
    QByteArray ret;
    ret.append(m_actionId);
    ret.append((quint8)m_type);
    ret.append(m_attributes.count());
    foreach (const TimelineAttribute &attr, m_attributes) {
        ret.append(attr.serialize());
    }
    return ret;
}
bool TimelineAction::deserialize(const QByteArray &data)
{
    WatchDataReader r(data);
    return deserialize(r);
}
bool TimelineAction::deserialize(WatchDataReader &r)
{
    if(r.checkBad(3)) return false;
    m_actionId = r.read<quint8>();
    m_type = (Type)r.read<quint8>();
    quint8 ac = r.read<quint8>();
    for(int i=0;i<ac;i++) {
        TimelineAttribute a;
        if(a.deserialize(r)) {
            m_attributes.append(a);
        } else
            return false;
    }
    return true;
}


void TimelineAttribute::setContent(const QByteArray &content)
{
    m_content = content;
}
QByteArray TimelineAttribute::getContent() const
{
    return m_content;
}

void TimelineAttribute::setByte(quint8 byte)
{
    m_content.append(byte);
}
quint8 TimelineAttribute::getByte() const
{
    return m_content.at(0);
}

void TimelineAttribute::setString(const QString &string, int max)
{
    m_content.append(string.toUtf8());
    if(max > 0 && m_content.length() > max)
        m_content.truncate(max);
}
QString TimelineAttribute::getString() const
{
    return QString::fromUtf8(m_content);
}

void TimelineAttribute::setStringList(const QStringList &values, int max)
{
    m_content.clear();
    for(int i=0;i<values.size();i++) {
        if(m_content.length()==max) {
            return;
        }
        QByteArray utf8Bytes = WatchDataWriter::chopStringToByteLength(values.at(i), max-m_content.length());
        m_content.append(utf8Bytes);
        if (i < values.size()-1) {
            m_content.append('\0');
        }

    }
}
QStringList TimelineAttribute::getStringList() const
{
    QStringList lst;
    foreach(const QByteArray ar,m_content.split('\0')) {
        lst.append(QString::fromUtf8(ar));
    }
    return lst;
}

QByteArray TimelineAttribute::serialize() const
{
    QByteArray ret;
    ret.append((quint8)m_type);
    ret.append(m_content.length() & 0xFF); ret.append(((m_content.length() >> 8) & 0xFF)); // length
    ret.append(m_content);
    return ret;
}
bool TimelineAttribute::deserialize(WatchDataReader &r)
{
    if(r.checkBad(3)) return false;
    m_type = r.read<quint8>();
    quint16 cl = r.readLE<quint16>();
    if(r.checkBad(cl)) return false;
    m_content = r.readBytes(cl);
    return true;
}
