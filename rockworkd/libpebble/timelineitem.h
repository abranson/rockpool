#ifndef TIMELINEITEM_H
#define TIMELINEITEM_H

#include <QByteArray>
#include <QDateTime>

#include "watchconnection.h"


class TimelineAttribute
{
public:
    enum Type {
        TypeTitle = 0x01,
        TypeSubtitle = 0x02,
        TypeBody = 0x03,
        TypeTinyIcon = 0x04,
        TypeLargeIcon = 0x06,
        TypeFieldNames = 0x19,
        TypeFieldValues = 0x1a,
        TypeColor = 0x1c,
        TypeRecurring = 0x1f
    };
    enum IconID {
        IconIDDefaultBell = 0x01,
        IconIDDefaultMissedCall = 0x02,
        IconIDReminder = 0x03,
        IconIDFlag = 0x04,
        IconIDWhatsApp = 0x05,
        IconIDTwitter = 0x06,
        IconIDTelegram = 0x07,
        IconIDHangout = 0x08,
        IconIDGMail = 0x09,
        IconIDFlash = 0x0a, // TODO: what service is this?
        IconIDFacebook = 0x0b,
        IconIDMusic = 0x0c,
        IconIDAlarm = 0x0d,
        IconIDWeather = 0x0e,
        IconIDGuess = 0x31
    };

    enum Color {
        ColorWhite = 0x00,
        ColorBlack = 0x80,
        ColorDarkBlue = 0x81,
        ColorBlue = 0x82,
        ColorLightBlue = 0x83,
        ColorDarkGreen = 0x84,
        ColorGray = 0x85,
        ColorBlue2 = 0x86,
        ColorLightBlue2 = 0x87,
        ColorGreen = 0x88,
        ColorOliveGreen = 0x89,
        ColorLightGreen = 0x90,
        ColorViolet = 0x91,
        ColorViolet2 = 0x91,
        ColorBlue3 = 0x92,
        ColorBrown = 0x93,
        ColorGray2 = 0x94,
        ColorBlue4 = 0x95,
        ColorBlue5 = 0x96,
        ColorRed = 0xA0,
        ColorOrange = 0xB8,
        ColorYellow = 0xBC
    };

    TimelineAttribute(Type type, const QByteArray &content):
        m_type(type),
        m_content(content)
    {}

    TimelineAttribute(Type type, IconID iconId):
        m_type(type)
    {
        setContent(iconId);
    }
    TimelineAttribute(Type type, Color color):
        m_type(type)
    {
        setContent(color);
    }
    TimelineAttribute(Type type, const QStringList &values):
        m_type(type)
    {
        setContent(values);
    }
    TimelineAttribute(Type type, quint8 data):
        m_type(type)
    {
        setContent(data);
    }

    void setContent(const QString &content);
    void setContent(IconID iconId);
    void setContent(Color color);
    void setContent(const QStringList &values);
    void setContent(quint8 data);

    QByteArray serialize() const;
private:
    Type m_type;
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
    quint8 m_layout = 0x01; // TODO: find out what this is about
    QList<TimelineAttribute> m_attributes;
    QList<TimelineAction> m_actions;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TimelineItem::Flags)

#endif // TIMELINEITEM_H
