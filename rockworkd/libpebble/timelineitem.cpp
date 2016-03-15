#include "timelineitem.h"

TimelineItem::TimelineItem(TimelineItem::Type type, Flags flags, const QDateTime &timestamp, quint16 duration):
    TimelineItem(QUuid::createUuid(), type, flags, timestamp, duration)
{

}

TimelineItem::TimelineItem(const QUuid &uuid, TimelineItem::Type type, Flags flags, const QDateTime &timestamp, quint16 duration):
    PebblePacket(),
    m_itemId(uuid),
    m_timestamp(timestamp),
    m_duration(duration),
    m_type(type),
    m_flags(flags)
{

}

QUuid TimelineItem::itemId() const
{
    return m_itemId;
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

void TimelineAttribute::setContent(const QString &content)
{
    m_content = content.toUtf8();
}

void TimelineAttribute::setContent(TimelineAttribute::IconID iconId)
{
    m_content.clear();
    m_content.append((quint8)iconId);
    m_content.append('\0');
    m_content.append('\0');
    m_content.append(0x80);
}

void TimelineAttribute::setContent(TimelineAttribute::Color color)
{
    m_content.clear();
    m_content.append((quint8)color);
}

void TimelineAttribute::setContent(const QStringList &values)
{
    m_content.clear();
    foreach (const QString &value, values) {
        if (!m_content.isEmpty()) {
            m_content.append('\0');
        }
        m_content.append(value.toUtf8());
    }
}

void TimelineAttribute::setContent(quint8 data)
{
    m_content.clear();
    m_content.append(data);
}

QByteArray TimelineAttribute::serialize() const
{
    QByteArray ret;
    ret.append((quint8)m_type);
    ret.append(m_content.length() & 0xFF); ret.append(((m_content.length() >> 8) & 0xFF)); // length
    ret.append(m_content);
    return ret;
}

