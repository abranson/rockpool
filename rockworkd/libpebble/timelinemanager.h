#ifndef TIMELINEMANAGER_H
#define TIMELINEMANAGER_H

#include "pebble.h"
#include "timelineitem.h"
#include <QObject>

class TimelineManager : public QObject
{
    Q_OBJECT
public:
    TimelineManager(Pebble *pebble, WatchConnection *onnection);

    void sendNotification(const Notification &notification);
    void insertTimelinePin(const QUuid &uuid, TimelineItem::Layout layout, bool isAllDay, const QDateTime &startTime, const QDateTime &endTime, const QString &title, const QString &description, const QMap<QString, QString> fields, bool recurring);
    void removeTimelinePin(const QUuid &uuid);
    void insertReminder(const QUuid &uuid, const QUuid &parentId, const QString &title, const QString &subtitle, const QString &body, const QDateTime &remindTime);
    void clearTimeline();
    void syncCalendar(const QList<CalendarEvent> &events);

signals:
    void muteSource(const QString &sourceId);
    void removeNotification(const QUuid &uuid);
    void actionTriggered(const QUuid &uuid, const QString &actToken);

private slots:
    void actionInvoked(const QByteArray &data);
private:
    QString m_timelineStoragePath;
    QHash<QUuid, Notification> m_notificationSources;

    QList<CalendarEvent> m_calendarEntries;
    CalendarEvent findCalendarEvent(const QString &id);

    Pebble *m_pebble;
    WatchConnection *m_connection;
};

#endif // TIMELINEMANAGER_H
