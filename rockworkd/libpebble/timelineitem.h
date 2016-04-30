#ifndef TIMELINEITEM_H
#define TIMELINEITEM_H

#include <QByteArray>
#include <QDateTime>

#include "watchconnection.h"


class TimelineAttribute
{
public:
    typedef quint32 ResID;
    /*
    enum Type {
        TypeTitle = 0x01,
        TypeSubtitle = 0x02,
        TypeBody = 0x03,
        TypeTinyIcon = 0x04,
        TypeSmallIcon = 0x05,
        TypeLargeIcon = 0x06,
        TypeFieldNames = 0x19,
        TypeFieldValues = 0x1a,
        TypeColor = 0x1c,
        TypeRecurring = 0x1f
    };
    enum IconID {
        IconIDNotificationGeneric = 1,
        IconIDTimelineMissedCall = 2,
        IconIDNotificationReminder = 3,
        IconIDNotificationFlag = 4,
        IconIDNotificationWhatsapp = 5,
        IconIDNotificationTwitter = 6,
        IconIDNotificationTelegram = 7,
        IconIDNotificationGoogleHangouts = 8,
        IconIDNotificationGMail = 9,
        IconIDNotificationFacebookMessenger = 10,
        IconIDNotificationFacebook = 11,
        IconIDAudioCasette = 12,
        IconIDAlarmClock = 13,
        IconIDTimelineWeather = 14,
        IconIDTimelineSun = 16,
        IconIDTimelineSports = 17,
        IconIDGenericEmail = 19,
        IconIDAmericanFootball = 20,
        IconIDTimelineCalendar = 21,
        IconIDTimelineBaseball = 22,
        IconIDBirthdayEvent = 23,
        IconIDCarRental = 24,
        IconIDCloudyDay = 25,
        IconIDCricketGame = 26,
        IconIDDinnerReservation = 27,
        IconIDGenericWarning = 28,
        IconIDGlucoseMonitor = 29,
        IconIDHockeyGame = 30,
        IconIDHotelReservation = 31,
        IconIDLightRain = 32,
        IconIDLightSnow = 33,
        IconIDMovieEvent = 34,
        IconIDMusicEvent = 35,
        IconIDNewsEvent = 36,
        IconIDPartlyCloudy = 37,
        IconIDPayBill = 38,
        IconIDRadioShow = 39,
        IconIDScheduledEvent = 40,
        IconIDSockerGame = 41,
        IconIDStocksEvent = 42,
        IconIDResultDeleted = 43,
        IconIDCheckInternetConnection = 44,
        IconIDGenericSMS = 45,
        IconIDResultMute = 46,
        IconIDResultSent = 47,
        IconIDWatchDisconnected = 48,
        IconIDDuringPhoneCall = 49,
        IconIDTideIsHigh = 50,
        IconIDResultDismissed = 51,
        IconIDHeavyRain = 52,
        IconIDHeavySnow = 53,
        IconIDScheduledFlight = 54,
        IconIDGenericConfirmation = 55,
        IconIDDaySeparator = 56,
        IconIDNoEvents = 57,
        IconIDNotificationBlackberryMessenger = 58,
        IconIDNotificationInstagram = 59,
        IconIDNotificationMailbox = 60,
        IconIDNotificationGoogleMailbox = 61,
        IconIDResultFailed = 62,
        IconIDGenericQuestion = 63,
        IconIDNotificationOutlook = 64,
        IconIDRainingAndSnowing = 65,
        IconIDReachedFitnessGoal = 66,
        IconIDNotificationLine = 67,
        IconIDNotificationSkype = 68,
        IconIDNotificationSnapchat = 69,
        IconIDNotificationViber = 70,
        IconIDNotificationWechat = 71,
        IconIDNotificationYahooMail = 72,
        IconIDTvShow = 73,
        IconIDBasketball = 74,
        IconIDDismissedPhoneCall = 75,
        IconIDNotificationGoogleMessenger = 76,
        IconIDNotificationHipchat = 77,
        IconIDIncomingPhoneCall = 78,
        IconIDNotificationKakaotalk = 79,
        IconIDNotificationKik = 80,
        IconIDNotificationLighthouse = 81,
        IconIDLocation = 82,
        IconIDSettings = 83,
        IconIDSunrise = 84,
        IconIDSunset = 85,
        IconIDResultUnmute = 86,
        IconIDResultUnmuteAlt = 94,
        IconIDDuringPhoneCallCentered = 95,
        IconIDTimelineEmptyCalendar = 96,
        IconIDThumbsUp = 97,
        IconIDArrowUp = 98,
        IconIDArrowDown = 99,
        IconIDActivity = 100,
        IconIDSleep = 101,
        IconIDRewardBad = 102,
        IconIDRewardGood = 103,
        IconIDRewardAverage = 104
    };
    */
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

    TimelineAttribute(quint8 type, const QByteArray &content):
        m_type(type),
        m_content(content)
    {}

    TimelineAttribute(quint8 type, ResID data):
        m_type(type)
    {
        setContent(data);
    }
    TimelineAttribute(quint8 type, Color color):
        m_type(type)
    {
        setContent(color);
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
    void setContent(quint32 data);
    void setContent(Color color);
    void setContent(const QStringList &values);
    void setContent(quint8 data);

    QByteArray serialize() const;
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
    quint8 m_layout = 0x01; // TODO: find out what this is about
    QList<TimelineAttribute> m_attributes;
    QList<TimelineAction> m_actions;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TimelineItem::Flags)

#endif // TIMELINEITEM_H
