#include "sailfishplatform.h"

#include "voicecallhandler.h"
#include "voicecallmanager.h"
#include "organizeradapter.h"
#include "syncmonitorclient.h"
#include "musiccontroller.h"
#include "notificationmonitor.h"

#include <QDBusConnection>
#include <QDebug>
#include <QSettings>

SailfishPlatform::SailfishPlatform(QObject *parent):
    PlatformInterface(parent)
{

    // Notifications
    m_notificationMonitor = new watchfish::NotificationMonitor(this);
    connect(m_notificationMonitor, &watchfish::NotificationMonitor::notification, this, &SailfishPlatform::onNotification);

    // Calls
    m_voiceCallManager = new VoiceCallManager(this);
    connect(m_voiceCallManager, SIGNAL(activeVoiceCallChanged()), SLOT(onActiveVoiceCallChanged()));

    // Music
    m_musicController = new watchfish::MusicController(this);
    connect(m_musicController, SIGNAL(metadataChanged()), SLOT(fetchMusicMetadata()));

    // Organizer
    m_organizerAdapter = new OrganizerAdapter(this);
    connect(m_organizerAdapter, &OrganizerAdapter::itemsChanged, this, &PlatformInterface::organizerItemsChanged);
}

void SailfishPlatform::onActiveVoiceCallChanged()
{

    VoiceCallHandler* handler = m_voiceCallManager->activeVoiceCall();
    if (handler) {
        connect(handler, SIGNAL(statusChanged()), SLOT(onActiveVoiceCallStatusChanged()));
        connect(handler, SIGNAL(destroyed()), SLOT(onActiveVoiceCallStatusChanged()));
        if (handler->status()) onActiveVoiceCallStatusChanged();
    }
}

void SailfishPlatform::onActiveVoiceCallStatusChanged()
{
    VoiceCallHandler* handler = m_voiceCallManager->activeVoiceCall();

    if (!handler || handler->handlerId() == nullptr) {
        return;
    }

    switch ((VoiceCallHandler::VoiceCallStatus)handler->status()) {
    case VoiceCallHandler::STATUS_ALERTING:
    case VoiceCallHandler::STATUS_DIALING:
        qDebug() << "Tell outgoing:" << handler->lineId();
        //emit outgoingCall(handlerId, handler->lineId(), m_voiceCallManager->findPersonByNumber(handler->lineId()));
        break;
    case VoiceCallHandler::STATUS_INCOMING:
    case VoiceCallHandler::STATUS_WAITING:
        qDebug() << "Tell incoming:" << handler->lineId();
        emit incomingCall(qHash(handler->handlerId()), handler->lineId(), m_voiceCallManager->findPersonByNumber(handler->lineId()));
        break;
    case VoiceCallHandler::STATUS_NULL:
    case VoiceCallHandler::STATUS_DISCONNECTED:
        qDebug() << "Endphone " << handler->handlerId();
        emit callEnded(qHash(handler->handlerId()), false);
        break;
    case VoiceCallHandler::STATUS_ACTIVE:
        qDebug() << "Startphone";
        emit callStarted(qHash(handler->handlerId()));
        break;
    case VoiceCallHandler::STATUS_HELD:
        break;
    }
}

void SailfishPlatform::onNotification(watchfish::Notification *notification) {

    qDebug() << "Got new notification for watch: " << notification->id();

    Notification n(notification->sender());
    if (notification->owner() == "twitter-notifications-client") {
        n.setType(Notification::NotificationTypeTwitter);
        n.setSender("Twitter");
    } else if (notification->category() == "x-nemo.email") {
        if (notification->sender().toLower().contains("gmail")) {
            n.setType(Notification::NotificationTypeGMail);
            n.setSender("GMail");
        }
        else {
            n.setType(Notification::NotificationTypeEmail);
            n.setSender(notification->sender());
        }
    } else if (notification->owner() == "facebook-notifications-client") {
        n.setType(Notification::NotificationTypeFacebook);
        n.setSender("Facebook");
    } else if (notification->category().startsWith("x-nemo.messaging.sms")) {
        n.setType(Notification::NotificationTypeSMS);
        n.setSender("SMS");
    } else if (notification->originPackage() == "org.telegram.messenger"
               || notification->category().startsWith("harbour.sailorgram")) {
        n.setType(Notification::NotificationTypeTelegram);
        n.setSender("Telegram");
    } else if (notification->originPackage() == "com.google.android.apps.babel"
               || notification->owner() == "harbour-hangish") {
        n.setType(Notification::NotificationTypeHangout);
        n.setSender("Hangouts");
    } else if (notification->originPackage() == "com.whatsapp"
               || notification->owner().toLower().contains("whatsup")) {
        n.setType(Notification::NotificationTypeWhatsApp);
        n.setSender("Whatsapp");
    } else if (notification->sender().contains("indicator-datetime")) {
        n.setType(Notification::NotificationTypeReminder);
        n.setSender("reminders");
    } else {
        n.setType(Notification::NotificationTypeGeneric);
    }
    n.setSubject(notification->summary());
    n.setBody(notification->body());
    foreach (const QString &action, notification->actions()) {
        if (action == "default") {
            n.setActToken(action);
            break;
        }
    }
    connect(notification, &watchfish::Notification::closed,
            this, &SailfishPlatform::handleClosedNotification);
    qDebug() << "have act token" << n.actToken();
    qDebug() << "mapping " << &n << " to " << notification;
    m_notifs.insert(n.uuid(), notification);
    emit notificationReceived(n);
}

void SailfishPlatform::handleClosedNotification(watchfish::Notification::CloseReason reason) {
    watchfish::Notification *n = static_cast<watchfish::Notification*>(sender());
    qDebug() << "Notification closed:" << n->id() << "Reason: " << reason;
    disconnect(n, 0, this, 0);
    QMap<QUuid, watchfish::Notification*>::iterator it = m_notifs.begin();
    while (it != m_notifs.end()) {
        if (it.value()->id() == n->id()) {
            qDebug() << "Found notification to remove " << it.key();
            emit notificationRemoved(it.key());
            m_notifs.remove(it.key());
            return;
        }
    }
    qDebug() << "Notification not found";
}

void SailfishPlatform::sendMusicControlCommand(MusicControlButton controlButton)
{
    switch (controlButton) {
    case MusicControlPlayPause:
        m_musicController->playPause();
        break;
    case MusicControlSkipBack:
        m_musicController->previous();
        break;
    case MusicControlSkipNext:
        m_musicController->next();
        break;
    case MusicControlVolumeUp:
        m_musicController->volumeUp();
        break;
    case MusicControlVolumeDown:
        m_musicController->volumeDown();
        break;
    default:
        ;
    }
}

MusicMetaData SailfishPlatform::musicMetaData() const
{
    return m_musicMetaData;
}

void SailfishPlatform::hangupCall(uint cookie)
{
    m_voiceCallManager->hangUp(cookie);
}

QList<CalendarEvent> SailfishPlatform::organizerItems() const
{
    return m_organizerAdapter->items();
}

void SailfishPlatform::actionTriggered(const QUuid &uuid, const QString &actToken) const
{
    qDebug() << "Triggering notification " << uuid << " action " << actToken;
    watchfish::Notification *notif = m_notifs.value(uuid);
    if (notif) {
        notif->invokeAction(actToken);
        removeNotification(uuid);
    }
    else
        qDebug() << "Not found";
}

void SailfishPlatform::removeNotification(const QUuid &uuid) const
{
    qDebug() << "Removing notification " << uuid;
    watchfish::Notification *notif = m_notifs.value(uuid);
    if (notif) {
        notif->close();
        m_notifs.remove(uuid);
    }
    else
        qDebug() << "Not found";
}

void SailfishPlatform::fetchMusicMetadata()
{
    m_musicMetaData.artist = m_musicController->artist();
    m_musicMetaData.album = m_musicController->album();
    m_musicMetaData.title = m_musicController->title();
    m_musicMetaData.duration = m_musicController->duration();
    emit musicMetadataChanged(m_musicMetaData);
}

void SailfishPlatform::mediaPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidatedProps)
{
    Q_UNUSED(interface)
    Q_UNUSED(changedProps)
    Q_UNUSED(invalidatedProps)
    fetchMusicMetadata();
}
