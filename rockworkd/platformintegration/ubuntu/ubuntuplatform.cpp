#include "ubuntuplatform.h"

#include "callchannelobserver.h"
#include "organizeradapter.h"
#include "syncmonitorclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>

// qmenumodel
#include "dbus-enums.h"
#include "liburl-dispatcher-1/url-dispatcher.h"

UbuntuPlatform::UbuntuPlatform(QObject *parent):
    PlatformInterface(parent),
    m_volumeActionGroup()
{
    // Notifications
    QDBusConnection::sessionBus().registerObject("/org/freedesktop/Notifications", this, QDBusConnection::ExportAllSlots);
    m_iface = new QDBusInterface("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_iface->call("AddMatch", "interface='org.freedesktop.Notifications',member='Notify',type='method_call',eavesdrop='true'");
    m_iface->call("AddMatch", "interface='org.freedesktop.Notifications',member='CloseNotification',type='method_call',eavesdrop='true'");

    // Music
    setupMusicService();
    m_volumeActionGroup.setBusType(DBusEnums::SessionBus);
    m_volumeActionGroup.setBusName("com.canonical.indicator.sound");
    m_volumeActionGroup.setObjectPath("/com/canonical/indicator/sound");
    m_volumeActionGroup.QDBusObject::connect();
    connect(&m_volumeActionGroup, &QDBusActionGroup::statusChanged, [this] {
        if (m_volumeActionGroup.status() == DBusEnums::Connected) {
            m_volumeAction = m_volumeActionGroup.action("volume");
        }
    });

    // Calls
    m_telepathyMonitor = new TelepathyMonitor(this);
    connect(m_telepathyMonitor, &TelepathyMonitor::incomingCall, this, &UbuntuPlatform::incomingCall);
    connect(m_telepathyMonitor, &TelepathyMonitor::callStarted, this, &UbuntuPlatform::callStarted);
    connect(m_telepathyMonitor, &TelepathyMonitor::callEnded, this, &UbuntuPlatform::callEnded);

    // Organizer
    m_organizerAdapter = new OrganizerAdapter(this);
    m_organizerAdapter->refresh();
    connect(m_organizerAdapter, &OrganizerAdapter::itemsChanged, this, &UbuntuPlatform::organizerItemsChanged);
    m_syncMonitorClient = new SyncMonitorClient(this);
    connect(m_syncMonitorClient, &SyncMonitorClient::stateChanged, [this]() { if (m_syncMonitorClient->state() == "idle") m_organizerAdapter->refresh();});
    m_syncTimer.start(1000 * 60 * 60);
    connect(&m_syncTimer, &QTimer::timeout, [this]() { m_syncMonitorClient->sync({"calendar"});});
    m_syncMonitorClient->sync({"calendar"});
}

QDBusInterface *UbuntuPlatform::interface() const
{
    return m_iface;
}

uint UbuntuPlatform::Notify(const QString &app_name, uint replaces_id, const QString &app_icon, const QString &summary, const QString &body, const QStringList &actions, const QVariantHash &hints, int expire_timeout)
{
    Q_UNUSED(replaces_id)
    // Lets directly suppress volume change notifications, network password entries and phone call snap decisions here
    QStringList hiddenNotifications = {"indicator-sound", "indicator-network"};
    if (!hiddenNotifications.contains(app_name)) {
        if (hints.contains("x-canonical-secondary-icon") && hints.value("x-canonical-secondary-icon").toString() == "incoming-call") {
            qDebug() << "Have a phone call notification. Ignoring it..." << app_name << app_icon;
        } else {
            qDebug() << "Notification received" << app_name << app_icon << actions << hints << expire_timeout;
            Notification n(app_name);
            if (app_name.contains("twitter")) {
                n.setType(Notification::NotificationTypeTwitter);
                n.setSourceName("Twitter");
            } else if (app_name.contains("dekko")) {
                n.setType(Notification::NotificationTypeEmail);
                n.setSourceName("EMail");
            } else if (app_name.toLower().contains("gmail")) {
                n.setType(Notification::NotificationTypeGMail);
                n.setSourceName("GMail");
            } else if (app_name.contains("facebook")) {
                n.setType(Notification::NotificationTypeFacebook);
                n.setSourceName("Facebook");
            } else if (app_name.contains("telegram")) {
                n.setType(Notification::NotificationTypeTelegram);
                n.setSourceName("Telegram");
            } else if (app_name.toLower().contains("hangout")) {
                n.setType(Notification::NotificationTypeHangout);
                n.setSourceName("Hangout");
            } else if (app_name.contains("indicator-datetime")) {
                n.setType(Notification::NotificationTypeReminder);
                n.setSourceName("reminders");
            } else {
                n.setType(Notification::NotificationTypeGeneric);
            }
            n.setSender(summary);
            n.setBody(body);
            foreach (const QString &action, actions) {
                if (action.contains(QRegExp("^[a-z]*://"))) {
                    n.setActToken(action);
                    break;
                }
            }
            qDebug() << "have act token" << n.actToken();

            emit notificationReceived(n);
        }
    }
    // We never return something. We're just snooping in...
    setDelayedReply(true);
    return 0;
}

void UbuntuPlatform::setupMusicService()
{
    if (!m_mprisService.isEmpty()) {
        disconnect(this, SLOT(mediaPropertiesChanged(QString,QVariantMap,QStringList)));
    }

    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    const QStringList &services = iface->registeredServiceNames();
    foreach (QString service, services) {
        if (service.startsWith("org.mpris.MediaPlayer2.")) {
            qDebug() << "have mpris service" << service;
            m_mprisService = service;
            fetchMusicMetadata();
            QDBusConnection::sessionBus().connect(m_mprisService, "/org/mpris/MediaPlayer2", "", "PropertiesChanged", this, SLOT(mediaPropertiesChanged(QString,QVariantMap,QStringList)));
            break;
        }
    }
}

void UbuntuPlatform::sendMusicControlCommand(MusicControlButton controlButton)
{
    if (m_mprisService.isEmpty()) {
        setupMusicService();
    }

    QString method;
    switch (controlButton) {
    case MusicControlPlayPause:
        method = "PlayPause";
        break;
    case MusicControlSkipBack:
        method = "Previous";
        break;
    case MusicControlSkipNext:
        method = "Next";
        break;
    default:
        ;
    }

    if (!method.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(m_mprisService, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", method);
        QDBusError err = QDBusConnection::sessionBus().call(call);

        if (err.isValid()) {
            qWarning() << "Error calling mpris method on" << m_mprisService << ":" << err.message();
        }
        return;
    }

    int volumeDiff = 0;
    switch (controlButton) {
    case MusicControlVolumeUp:
        volumeDiff = 1;
        break;
    case MusicControlVolumeDown:
        volumeDiff = -1;
        break;
    default:
        ;
    }

    if (m_volumeAction && volumeDiff != 0) {
        m_volumeAction->activate(volumeDiff);
        return;
    }
}

MusicMetaData UbuntuPlatform::musicMetaData() const
{
    return m_musicMetaData;
}

void UbuntuPlatform::hangupCall(uint cookie)
{
    m_telepathyMonitor->hangupCall(cookie);
}

QList<CalendarEvent> UbuntuPlatform::organizerItems() const
{
    return m_organizerAdapter->items();
}

void UbuntuPlatform::actionTriggered(const QString &actToken)
{
    url_dispatch_send(actToken.toStdString().c_str(), [] (const gchar *, gboolean, gpointer) {}, nullptr);
}

void UbuntuPlatform::fetchMusicMetadata()
{
    if (!m_mprisService.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(m_mprisService, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get");
        call << "org.mpris.MediaPlayer2.Player" << "Metadata";
        QDBusPendingCall pcall = QDBusConnection::sessionBus().asyncCall(call);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pcall, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, &UbuntuPlatform::fetchMusicMetadataFinished);
    }
}

void UbuntuPlatform::fetchMusicMetadataFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    QDBusReply<QDBusVariant> reply = watcher->reply();
    if (reply.isValid()) {
        QVariantMap curMetadata = qdbus_cast<QVariantMap>(reply.value().variant().value<QDBusArgument>());
        m_musicMetaData.artist = curMetadata.value("xesam:artist").toString();
        m_musicMetaData.album = curMetadata.value("xesam:album").toString();
        m_musicMetaData.title = curMetadata.value("xesam:title").toString();
        emit musicMetadataChanged(m_musicMetaData);
    } else {
        qWarning() << reply.error().message();
    }
}

void UbuntuPlatform::mediaPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidatedProps)
{
    Q_UNUSED(interface)
    Q_UNUSED(changedProps)
    Q_UNUSED(invalidatedProps)
    fetchMusicMetadata();
}
