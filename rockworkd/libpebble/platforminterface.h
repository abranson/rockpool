#ifndef PLATFORMINTERFACE_H
#define PLATFORMINTERFACE_H

#include "libpebble/pebble.h"
#include "libpebble/musicmetadata.h"

#include <QObject>
#include <QJsonDocument>

static const QUuid uuid_ns_dns("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
class PlatformInterface: public QObject
{
    Q_OBJECT
public:
    PlatformInterface(QObject *parent = 0): QObject(parent) {}
    virtual ~PlatformInterface() {}
    // App Specific Resources for Pins. Initialized in core.cpp
    // type: {icon, color, [mute_name, [...]]}
    static const QHash<QString,QStringList> AppResMap;
    static inline QUuid idToGuid(QString id) {
        return QUuid::createUuidV5(uuid_ns_dns,QString("%1.pin.rockpool.nemomobile.org").arg(id));
    }

// Device state
public:
    virtual bool deviceIsActive() const = 0;
    virtual void setProfile(const QString &profile) const = 0;
signals:
    void deviceActiveChanged();

// Notifications
public:
    virtual void actionTriggered(const QUuid &uuid, const QString &actToken) const = 0;
    virtual void removeNotification(const QUuid &uuid) const = 0;
signals:
    void notificationReceived(const Notification &notification);
    void notificationRemoved(const QUuid &uuid);
    void musicPlayStateChanged(const MusicPlayState &playState);
    void timeChanged();
    void newTimelinePin(QJsonDocument pin);

// Music
public:
    virtual void sendMusicControlCommand(MusicControlButton controlButton) = 0;
    virtual MusicMetaData musicMetaData() const = 0;
    virtual MusicPlayState getMusicPlayState() const = 0;

signals:
    void musicMetadataChanged(MusicMetaData metaData);

// Phone calls
signals:
    void incomingCall(uint cookie, const QString &number, const QString &name);
    void callStarted(uint cookie);
    void callEnded(uint cookie, bool missed);
public:
    virtual void hangupCall(uint cookie) = 0;

// Organizer
public:
    virtual QList<CalendarEvent> organizerItems() const = 0;
signals:
    void organizerItemsChanged(const QList<CalendarEvent> &items);

};

#endif // PLATFORMINTERFACE_H
