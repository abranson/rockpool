#ifndef TIMELINEMANAGER_H
#define TIMELINEMANAGER_H

#include "pebble.h"
#include "timelineitem.h"
#include <QObject>

struct Attr {
    quint8 id;
    quint8 max;
    QString type;
    QString note;
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

    void sendNotification(const Notification &notification);
    void insertTimelinePin(const QUuid &uuid, TimelineItem::Layout layout, bool isAllDay, const QDateTime &startTime, const QDateTime &endTime, const QString &title, const QString &description, const QMap<QString, QString> fields, bool recurring);
    void removeTimelinePin(const QUuid &uuid);
    void insertReminder(const QUuid &uuid, const QUuid &parentId, const QString &title, const QString &subtitle, const QString &body, const QDateTime &remindTime);
    void clearTimeline();
    void syncCalendar(const QList<CalendarEvent> &events);

    TimelineAttribute::ResID getRes(const QString &key) const;
    quint8 getLayout(const QString &key) const;
    Attr getAttr(const QString &key) const;
signals:
    void muteSource(const QString &sourceId);
    void removeNotification(const QUuid &uuid);
    void actionTriggered(const QUuid &uuid, const QString &actToken);

private slots:
    void actionInvoked(const QByteArray &data);
private:
    QString m_timelineStoragePath;
    QHash<QUuid, Notification> m_notificationSources;
    QHash<QString,quint8> m_layouts;
    QHash<QString,qint32> m_resources;
    QHash<QString,Attr> m_attributes;

    QList<CalendarEvent> m_calendarEntries;
    CalendarEvent findCalendarEvent(const QString &id);

    Pebble *m_pebble;
    WatchConnection *m_connection;
};

#endif // TIMELINEMANAGER_H
