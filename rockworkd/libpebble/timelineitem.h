#ifndef TIMELINEITEM_H
#define TIMELINEITEM_H

#include <QByteArray>
#include <QDateTime>

#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

class TimelineAttribute
{
public:
    TimelineAttribute(quint8 type, const QByteArray &content):
        m_type(type),
        m_content(content)
    {}

    TimelineAttribute(quint8 type, const QStringList &values):
        m_type(type)
    {
        setStringList(values);
    }
    TimelineAttribute(quint8 type, quint32 data):
        m_type(type)
    {
        setInt<quint32>(data);
    }/*
    TimelineAttribute(quint8 type, quint8 data):
        m_type(type)
    {
        setByte(data);
    }*/
    TimelineAttribute(quint8 type, const QString &string):
        m_type(type)
    {
        setString(string);
    }
    TimelineAttribute(quint8 type):
        m_type(type)
    {}
    TimelineAttribute():
        m_type(0)
    {}
    quint8 type() const { return m_type;}

    template <typename T>
    void setInt(T data) {
        WatchDataWriter w(&m_content);
        w.writeLE<T>(data);
    }
    template <typename T>
    T getInt() const {
        WatchDataReader r(m_content);
        return r.readLE<T>();
    }

    void setByte(quint8 byte);
    quint8 getByte() const;

    void setStringList(const QStringList &values, int max = 0);
    QStringList getStringList() const;

    void setString(const QString &string, int max = 0);
    QString getString() const;

    void setContent(const QByteArray &content);
    QByteArray getContent() const;

    QByteArray serialize() const;
    bool deserialize(WatchDataReader &r);

private:
    quint8 m_type;
    QByteArray m_content;
};

class TimelineAction: public PebblePacket
{
public:
    enum Type {
        TypeInvalid = 0,
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
    TimelineAction():
        m_actionId(0),
        m_type(TypeInvalid)
    {}
    TimelineAction(quint8 actionId, Type type, const QList<TimelineAttribute> &attributes = QList<TimelineAttribute>());
    void appendAttribute(const TimelineAttribute &attribute);

    QByteArray serialize() const override;
    bool deserialize(const QByteArray &data) override;
    bool deserialize(WatchDataReader &r);

private:
    quint8 m_actionId;
    Type m_type;
    QList<TimelineAttribute> m_attributes;
};

class TimelineItem: public BlobDbItem
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

    TimelineItem();
    TimelineItem(Type type, TimelineItem::Flags flags = FlagNone, const QDateTime &timestamp = QDateTime::currentDateTime(), quint16 duration = 0);
    TimelineItem(const QUuid &uuid, Type type, Flags flags = FlagNone, const QDateTime &timestamp = QDateTime::currentDateTime(), quint16 duration = 0);

    QUuid itemId() const;
    QDateTime ts() const;

    void setParentId(QUuid parentId);
    void setLayout(quint8 layout);
    void setFlags(Flags flags);

    void appendAttribute(const TimelineAttribute &attribute);
    void appendAction(const TimelineAction &action);

    QList<TimelineAttribute> attributes() const;
    QList<TimelineAction> actions() const;

    QByteArray itemKey() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray &data) override;

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
