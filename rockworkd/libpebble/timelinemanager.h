#ifndef TIMELINEMANAGER_H
#define TIMELINEMANAGER_H

#include "pebble.h"
#include "timelineitem.h"
#include <QObject>

struct Attr {
    quint8 id;
    quint16 max;
    QString type;
    QString note;
    QHash<QString,quint8> enums;
};
struct ResMap {
    QString icon;
    QString mute;
    int color;
};

class TimelineManager : public QObject
{
    Q_OBJECT
public:
    static const ResMap NotificationMap[16];

    TimelineManager(Pebble *pebble, WatchConnection *onnection);

    // Timeline Layout
    quint32 getRes(const QString &key) const;
    quint8 getLayout(const QString &key) const;
    Attr getAttr(const QString &key) const;
    // New Timeline API
    void insertTimelinePin(const QJsonDocument &json);
    void sendNotification(const QJsonObject &pinMap);
    // Old Direct API
    void sendNotification(const Notification &notification);
    void insertTimelinePin(const QUuid &uuid, TimelineItem::Layout layout, bool isAllDay, const QDateTime &startTime, const QDateTime &endTime, const QString &title, const QString &description, const QMap<QString, QString> fields, bool recurring);
    void removeTimelinePin(const QUuid &uuid);
    void insertReminder(const QUuid &uuid, const QUuid &parentId, const QString &title, const QString &subtitle, const QString &body, const QDateTime &remindTime);
    void clearTimeline();
    void syncCalendar(const QList<CalendarEvent> &events);

public slots:
    void reloadLayouts();

signals:
    void actionRequested(const QUuid &uuid, quint32 actionId, quint8 param);
    void muteSource(const QString &sourceId);
    void removeNotification(const QUuid &uuid);
    void actionTriggered(const QUuid &uuid, const QString &actToken);

private slots:
    void actionInvoked(const QByteArray &data);
private:
    QString m_timelineStoragePath;
    QHash<QUuid, QStringList> m_notificationSources;
    TimelineItem & parseLayout(TimelineItem &timelineItem, const QJsonObject &layout);
    TimelineItem & createActions(TimelineItem &timelineItem, const QJsonObject &pinMap);

    QHash<QString,quint8> m_layouts;
    QHash<QString,qint32> m_resources;
    QHash<QString,Attr> m_attributes;

    QList<CalendarEvent> m_calendarEntries;
    CalendarEvent findCalendarEvent(const QString &id);

    Pebble *m_pebble;
    WatchConnection *m_connection;
};

#endif // TIMELINEMANAGER_H
