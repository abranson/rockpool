#ifndef SAILFISHPLATFORM_H
#define SAILFISHPLATFORM_H

#include "libpebble/platforminterface.h"
#include "libpebble/enums.h"
#include "voicecallmanager.h"
#include "voicecallhandler.h"
#include "musiccontroller.h"
#include "notificationmonitor.h"
#include "walltimemonitor.h"

#include <QDBusInterface>
#include <QDBusContext>

class QDBusPendingCallWatcher;
class VoiceCallManager;
class OrganizerAdapter;

class SailfishPlatform : public PlatformInterface, public QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")


public:
    SailfishPlatform(QObject *parent = 0);

    void sendMusicControlCommand(MusicControlButton controlButton) override;
    MusicMetaData musicMetaData() const override;
    void hangupCall(uint cookie) override;
    QHash<QString, QString> getCategoryParams(QString category);

    QList<CalendarEvent> organizerItems() const override;
    void actionTriggered(const QUuid &uuid, const QString &actToken) const override;
    void removeNotification(const QUuid &uuid) const override;

public slots:
    void onNotification(watchfish::Notification *notification);
    void handleClosedNotification(watchfish::Notification::CloseReason reason);
    void onTimeChanged();

private slots:
    void fetchMusicMetadata();
    void mediaPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidatedProps);

    void onActiveVoiceCallChanged();
    void onActiveVoiceCallStatusChanged();

private:
    QDBusInterface *m_iface;
    MusicMetaData m_musicMetaData;
    VoiceCallManager *m_voiceCallManager;
    OrganizerAdapter *m_organizerAdapter;
    mutable QMap<QUuid, watchfish::Notification*> m_notifs;
    watchfish::MusicController *m_musicController;
    watchfish::NotificationMonitor *m_notificationMonitor;
    watchfish::WallTimeMonitor *m_wallTimeMonitor;
};

#endif // SAILFISHPLATFORM_H
