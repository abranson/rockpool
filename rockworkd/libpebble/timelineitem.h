#ifndef TIMELINEITEM_H
#define TIMELINEITEM_H

#include <QByteArray>
#include <QDateTime>

#include "watchconnection.h"


class TimelineAttribute
{
public:
    typedef quint32 le32;
    typedef quint16 le16;

    TimelineAttribute(quint8 type, const QByteArray &content):
        m_type(type),
        m_content(content)
    {}

    TimelineAttribute(quint8 type, quint32 data):
        m_type(type)
    {
        setContent(data);
    }
    TimelineAttribute(quint8 type, const QStringList &values):
        m_type(type)
    {
        setContent(values);
    }
    TimelineAttribute(quint8 type, quint8 data):
        m_type(type)
    {
        setContent(data);
    }

    void setContent(const QString &content);
    void setContent(qint32 data);
    void setContent(qint16 data);
    void setContent(quint32 data);
    void setContent(quint16 data);
    void setContent(const QStringList &values);
    void setContent(quint8 data);
    void setContent(le32 *data);
    void setContent(le16 *data);

    QByteArray serialize() const;
    quint8 type() { return m_type;}
private:
    quint8 m_type;
    QByteArray m_content;
};

class TimelineAction: public PebblePacket
{
public:
    enum Type {
        TypeAncsDismiss = 1,
        TypeGeneric = 2,
        TypeResponse = 3,
        TypeDismiss = 4,
        TypeHTTP = 5,
        TypeSnooze = 6,
        TypeOpenWatchApp = 7,
        TypeEmpty = 8,
        TypeRemove = 9,
        TypeOpenPin = 10
    };
    TimelineAction(quint8 actionId, Type type, const QList<TimelineAttribute> &attributes = QList<TimelineAttribute>());
    void appendAttribute(const TimelineAttribute &attribute);

    QByteArray serialize() const override {
        QByteArray ret;
        ret.append(m_actionId);
        ret.append((quint8)m_type);
        ret.append(m_attributes.count());
        foreach (const TimelineAttribute &attr, m_attributes) {
            ret.append(attr.serialize());
        }
        return ret;
    }

private:
    quint8 m_actionId;
    Type m_type;
    QList<TimelineAttribute> m_attributes;
};

class TimelineItem: public PebblePacket
{
public:
    enum Type {
        TypeInvalid = 0,
        TypeNotification = 1,
        TypePin = 2,
        TypeReminder = 3
    };

    // TODO: this is probably not complete and maybe even wrong.
    enum Flag {
        FlagNone = 0x00,
        FlagSingleEvent = 0x01,
        FlagTimeInUTC = 0x02,
        FlagAllDay = 0x04
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    // TODO: This is not complete
    enum Layout {
        LayoutGenericPin = 0x01,
        LayoutCalendar = 0x02
    };

    TimelineItem(Type type, TimelineItem::Flags flags = FlagNone, const QDateTime &timestamp = QDateTime::currentDateTime(), quint16 duration = 0);
    TimelineItem(const QUuid &uuid, Type type, Flags flags = FlagNone, const QDateTime &timestamp = QDateTime::currentDateTime(), quint16 duration = 0);

    QUuid itemId() const;

    void setParentId(QUuid parentId);
    void setLayout(quint8 layout);
    void setFlags(Flags flags);

    void appendAttribute(const TimelineAttribute &attribute);
    void appendAction(const TimelineAction &action);

    QList<TimelineAttribute> attributes() const;
    QList<TimelineAction> actions() const;

    QByteArray serialize() const override;

private:
    QUuid m_itemId;
    QUuid m_parentId;
    QDateTime m_timestamp;
    quint16 m_duration = 0;
    Type m_type;
    Flags m_flags; // quint16
    quint8 m_layout = 0x01;
    QList<TimelineAttribute> m_attributes;
    QList<TimelineAction> m_actions;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TimelineItem::Flags)

#endif // TIMELINEITEM_H
