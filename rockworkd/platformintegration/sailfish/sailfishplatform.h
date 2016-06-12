#ifndef SAILFISHPLATFORM_H
#define SAILFISHPLATFORM_H

#include "libpebble/platforminterface.h"
#include "libpebble/enums.h"
#include "voicecallmanager.h"
#include "voicecallhandler.h"
#include "musiccontroller.h"
#include "notificationmonitor.h"
#include "walltimemonitor.h"
#include "modecontrolentity.h"

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
    ~SailfishPlatform();

    void sendMusicControlCommand(MusicControlButton controlButton) override;
    MusicMetaData musicMetaData() const override;
    void hangupCall(uint cookie) override;
    QHash<QString, QString> getCategoryParams(QString category);

    void syncOrganizer(quint32 end) const override;
    void stopOrganizer() const override;
    MusicPlayState getMusicPlayState() const override;

    void actionTriggered(const QUuid &uuid, const QString &actToken, const QJsonObject &param) const override;
    void removeNotification(const QUuid &uuid) const override;
    bool deviceIsActive() const override;
    void setProfile(const QString &profile) const override;

    const QHash<QString,QStringList>& cannedResponses() const override;
    void setCannedResponses(const QHash<QString, QStringList> &cans) override;

public slots:
    void telepathyResponse(const watchfish::Notification *n, const QJsonObject &param) const;
    void newNotificationPin(watchfish::Notification *notification);
    void handleClosedNotification(watchfish::Notification::CloseReason reason);
    void onTimeChanged();
    void updateMusicStatus();

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
    ModeControlEntity *m_nokiaMCE;
    mutable QMap<QUuid, watchfish::Notification*> m_notifs;
    watchfish::MusicController *m_musicController;
    watchfish::NotificationMonitor *m_notificationMonitor;
    watchfish::WallTimeMonitor *m_wallTimeMonitor;
    QHash<QString,QStringList> m_cans;
};

#endif // SAILFISHPLATFORM_H
