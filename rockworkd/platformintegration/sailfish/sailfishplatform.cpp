#include "sailfishplatform.h"

#include "voicecallhandler.h"
#include "voicecallmanager.h"
#include "organizeradapter.h"
#include "syncmonitorclient.h"
#include "musiccontroller.h"
#include "notificationmonitor.h"
#include "walltimemonitor.h"

#include <QDBusConnection>
#include <QDebug>
#include <QSettings>

SailfishPlatform::SailfishPlatform(QObject *parent):
    PlatformInterface(parent)
{
    // Time sync
    m_wallTimeMonitor = new watchfish::WallTimeMonitor(this);
    connect(m_wallTimeMonitor, &watchfish::WallTimeMonitor::timeChanged, this, &SailfishPlatform::onTimeChanged);

    // Notifications
    m_notificationMonitor = new watchfish::NotificationMonitor(this);
    connect(m_notificationMonitor, &watchfish::NotificationMonitor::notification, this, &SailfishPlatform::onNotification);

    // Calls
    m_voiceCallManager = new VoiceCallManager(this);
    connect(m_voiceCallManager, SIGNAL(activeVoiceCallChanged()), SLOT(onActiveVoiceCallChanged()));

    // Music
    m_musicController = new watchfish::MusicController(this);
    connect(m_musicController, SIGNAL(metadataChanged()), SLOT(fetchMusicMetadata()));
    connect(m_musicController, SIGNAL(statusChanged()), SLOT(updateMusicStatus()));
    connect(m_musicController, SIGNAL(positionChanged()), SLOT(updateMusicStatus()));

    // Organizer
    m_organizerAdapter = new OrganizerAdapter(this);
    connect(m_organizerAdapter, &OrganizerAdapter::itemsChanged, this, &PlatformInterface::organizerItemsChanged);
    connect(m_wallTimeMonitor, &watchfish::WallTimeMonitor::timezoneChanged, m_organizerAdapter, &OrganizerAdapter::scheduleRefresh);

    // Device - MCE
    m_nokiaMCE = new ModeControlEntity(this);
}
SailfishPlatform::~SailfishPlatform()
{
    delete m_nokiaMCE;
    delete m_organizerAdapter;
    delete m_musicController;
    delete m_voiceCallManager;
    delete m_notificationMonitor;
    delete m_wallTimeMonitor;
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

void SailfishPlatform::onTimeChanged() {
    emit timeChanged();
}

void SailfishPlatform::onNotification(watchfish::Notification *notification) {

    qDebug() << "Got new notification for watch: " << notification->owner() << notification->summary();

    //HACK: ignore group notifications
   if (notification->category().endsWith(".group")) {
       qDebug() << "Skipping group notification.";
       return;
   }

    QString owner = notification->originPackage().isEmpty()?notification->owner():notification->originPackage();
    if (owner.isEmpty()) owner = notification->sender();
    Notification n(owner);
    if (notification->owner() == "twitter-notifications-client") {
        n.setType(Notification::NotificationTypeTwitter);
        n.setSourceId("Twitter");
    } else if (notification->category() == "x-nemo.email") {
        if (notification->sender().toLower().contains("gmail")) {
            n.setType(Notification::NotificationTypeGMail);
            n.setSender("GMail");
        }
        else {
            n.setType(Notification::NotificationTypeEmail);
            n.setSender(notification->sender());
            n.setSourceId(notification->sender());
        }
    } else if (notification->owner() == "facebook-notifications-client") {
        n.setType(Notification::NotificationTypeFacebook);
        n.setSender("Facebook");
    } else if (notification->category() == "x-nemo.messaging.sms") {
        n.setType(Notification::NotificationTypeSMS);
        n.setSender("SMS");
        n.setSourceId("SMS");
    } else if (notification->category() == "x-nemo.messaging.im") {
        n.setType(Notification::NotificationTypeSMS);
        n.setSender("Instant Message");
        n.setSourceId(notification->sender());
    } else if (notification->originPackage() == "org.telegram.messenger"
               || notification->category().startsWith("harbour.sailorgram")) {
        n.setType(Notification::NotificationTypeTelegram);
    } else if (notification->originPackage() == "com.google.android.apps.babel"
               || notification->owner() == "harbour-hangish") {
        n.setType(Notification::NotificationTypeHangout);
    } else if (notification->originPackage() == "com.whatsapp"
               || notification->owner().toLower().contains("whatsup")) {
        n.setType(Notification::NotificationTypeWhatsApp);
    } else {
        n.setType(Notification::NotificationTypeGeneric);
        n.setSender(notification->sender());
    }
    n.setSubject(notification->summary());
    n.setBody(notification->body());
    if (!notification->icon().startsWith("/opt/alien/data/notificationIcon/")) //these are temporary, don't store them
        n.setIcon(notification->icon());

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
        it++;
    }
    qDebug() << "Notification not found";
}

void SailfishPlatform::sendMusicControlCommand(MusicControlButton controlButton)
{
    switch (controlButton) {
    case MusicControlPlayPause:
        m_musicController->playPause();
        break;
    case MusicControlPause:
        m_musicController->pause();
        break;
    case MusicControlPlay:
        m_musicController->play();
        break;
    case MusicControlPreviousTrack:
        m_musicController->previous();
        break;
    case MusicControlNextTrack:
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
    qDebug() << "New track " << m_musicMetaData.title << ". Length: " << m_musicMetaData.duration;
    emit musicMetadataChanged(m_musicMetaData);
    updateMusicStatus();
}

void SailfishPlatform::updateMusicStatus() {
    MusicPlayState playState = getMusicPlayState();
    emit musicPlayStateChanged(playState);
}



MusicPlayState SailfishPlatform::getMusicPlayState() const {
    MusicPlayState playState;
    switch (m_musicController->status()) {
        case watchfish::MusicController::Status::StatusNoPlayer:
            playState.state = MusicPlayState::StateUnknown;
        break;
        case watchfish::MusicController::Status::StatusStopped:
            playState.state = MusicPlayState::StatePaused;
        break;
        case watchfish::MusicController::Status::StatusPaused:
            playState.state = MusicPlayState::StatePaused;
        break;
        case watchfish::MusicController::Status::StatusPlaying:
            playState.state = MusicPlayState::StatePlaying;
        break;
        default:
            playState.state = MusicPlayState::StateUnknown;
    }
    switch (m_musicController->repeat()) {
        case watchfish::MusicController::RepeatStatus::RepeatNone:
            playState.repeat = MusicPlayState::RepeatOff;
        break;
        case watchfish::MusicController::RepeatStatus::RepeatTrack:
            playState.repeat = MusicPlayState::RepeatOne;
        break;
        case watchfish::MusicController::RepeatStatus::RepeatPlaylist:
            playState.repeat = MusicPlayState::RepeatAll;
        break;
        default:
            playState.repeat = MusicPlayState::RepeatUnknown;
    }

    playState.trackPosition = m_musicController->position()/1000;
    if (m_musicController->shuffle())
        playState.shuffle = MusicPlayState::ShuffleOn;
    else
        playState.shuffle = MusicPlayState::ShuffleOff;

    return playState;
}

void SailfishPlatform::mediaPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidatedProps)
{
    Q_UNUSED(interface)
    Q_UNUSED(changedProps)
    Q_UNUSED(invalidatedProps)
    fetchMusicMetadata();
}

bool SailfishPlatform::deviceIsActive() const
{
    return m_nokiaMCE->isActive();
}

void SailfishPlatform::setProfile(const QString &profile) const
{
    QDBusReply<bool> res = QDBusConnection::sessionBus().call(
                QDBusMessage::createMethodCall("com.nokia.profiled", "/com/nokia/profiled", "com.nokia.profiled", "set_profile")
                << profile);
    if (res.isValid()) {
        if (!res.value()) {
            qWarning() << "Unable to set profile" << profile;
        }
    }
    else {
        qWarning() << res.error().message();
    }
}
