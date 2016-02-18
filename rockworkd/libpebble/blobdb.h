#ifndef BLOBDB_H
#define BLOBDB_H

#include "watchconnection.h"
#include "pebble.h"
#include "timelineitem.h"
#include "healthparams.h"
#include "appmetadata.h"

#include <QObject>
#include <QDateTime>
#include <QOrganizerEvent>

QTORGANIZER_USE_NAMESPACE


class BlobDB : public QObject
{
    Q_OBJECT
public:
    enum BlobDBId {
        BlobDBIdTest = 0,
        BlobDBIdPin = 1,
        BlobDBIdApp = 2,
        BlobDBIdReminder = 3,
        BlobDBIdNotification = 4,
        BlobDBIdAppSettings = 7

    };
    enum Operation {
        OperationInsert = 0x01,
        OperationDelete = 0x04,
        OperationClear = 0x05
    };

    enum Status {
        StatusSuccess = 0x00,
        StatusError = 0x01
    };


    explicit BlobDB(Pebble *pebble, WatchConnection *connection);

    void insertNotification(const Notification &notification);
    void insertTimelinePin(const QUuid &uuid, TimelineItem::Layout layout, bool isAllDay, const QDateTime &startTime, const QDateTime &endTime, const QString &title, const QString &desctiption, const QMap<QString, QString> fields, bool recurring);
    void removeTimelinePin(const QUuid &uuid);
    void insertReminder();
    void clearTimeline();
    void syncCalendar(const QList<CalendarEvent> &events);

    void clearApps();
    void insertAppMetaData(const AppInfo &info);
    void removeApp(const AppInfo &info);

    void insert(BlobDBId database, const TimelineItem &item);
    void remove(BlobDBId database, const QUuid &uuid);
    void clear(BlobDBId database);

    void setHealthParams(const HealthParams &healthParams);
    void setUnits(bool imperial);

private slots:
    void blobCommandReply(const QByteArray &data);
    void actionInvoked(const QByteArray &data);
    void sendActionReply();
    void sendNext();

signals:
    void muteSource(const QString &sourceId);
    void actionTriggered(const QString &actToken);
    void appInserted(const QUuid &uuid);

private:
    quint16 generateToken();
    AppMetadata appInfoToMetadata(const AppInfo &info, HardwarePlatform hardwarePlatform);

private:

    class BlobCommand: public PebblePacket
    {
    public:
        BlobDB::Operation m_command; // quint8
        quint16 m_token;
        BlobDB::BlobDBId m_database;

        QByteArray m_key;
        QByteArray m_value;

        QByteArray serialize() const override;
    };

    Pebble *m_pebble;
    WatchConnection *m_connection;

    QHash<QUuid, Notification> m_notificationSources;

    QList<CalendarEvent> m_calendarEntries;
    CalendarEvent findCalendarEvent(const QString &id);

    HealthParams m_healthParams;

    BlobCommand *m_currentCommand = nullptr;
    QList<BlobCommand*> m_commandQueue;

    QString m_blobDBStoragePath;
};

#endif // BLOBDB_H
