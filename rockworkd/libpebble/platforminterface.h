#ifndef PLATFORMINTERFACE_H
#define PLATFORMINTERFACE_H

#include "libpebble/pebble.h"
#include "libpebble/musicmetadata.h"

#include <QObject>
#include <QOrganizerItem>

class PlatformInterface: public QObject
{
    Q_OBJECT
public:
    PlatformInterface(QObject *parent = 0): QObject(parent) {}
    virtual ~PlatformInterface() {}

// Notifications
public:
    virtual void actionTriggered(const QUuid &uuid, const QString &actToken) const = 0;
    virtual void removeNotification(const QUuid &uuid) const = 0;
signals:
    void notificationReceived(const Notification &notification);
    void notificationRemoved(const QUuid &uuid);

// Music
public:
    virtual void sendMusicControlCommand(MusicControlButton controlButton) = 0;
    virtual MusicMetaData musicMetaData() const = 0;
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
