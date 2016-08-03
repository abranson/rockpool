#include "sailfishplatform.h"

#include "voicecallhandler.h"
#include "voicecallmanager.h"
#include "organizeradapter.h"
#include "musiccontroller.h"
#include "notificationmonitor.h"
#include "walltimemonitor.h"

#include <QDBusConnection>
#include <QDebug>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

SailfishPlatform::SailfishPlatform(QObject *parent):
    PlatformInterface(parent)
{
    // canned response prereqs: telepathy message type and msg sources we can handle
    qDBusRegisterMetaType<QList<QVariantMap>>();
    m_cans.insert("x-nemo.messaging.im",QStringList());
    m_cans.insert("x-nemo.messaging.sms",QStringList());
    // Time sync
    m_wallTimeMonitor = new watchfish::WallTimeMonitor(this);
    connect(m_wallTimeMonitor, &watchfish::WallTimeMonitor::timeChanged, this, &SailfishPlatform::onTimeChanged);

    // Notifications
    m_notificationMonitor = new watchfish::NotificationMonitor(this);
    //connect(m_notificationMonitor, &watchfish::NotificationMonitor::notification, this, &SailfishPlatform::onNotification);
    connect(m_notificationMonitor, &watchfish::NotificationMonitor::notification, this, &SailfishPlatform::newNotificationPin);

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
    connect(m_organizerAdapter, &OrganizerAdapter::newTimelinePin, this, &PlatformInterface::newTimelinePin);
    connect(m_organizerAdapter, &OrganizerAdapter::delTimelinePin, this, &PlatformInterface::delTimelinePin);
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

    if (!handler || handler->handlerId().isNull()) {
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
        if(handler->getState() < VoiceCallHandler::StateRinging) {
            handler->setState(VoiceCallHandler::StateRinging);
            emit incomingCall(qHash(handler->handlerId()), handler->lineId(), m_voiceCallManager->findPersonByNumber(handler->lineId()));
        }
        break;
    case VoiceCallHandler::STATUS_NULL:
    case VoiceCallHandler::STATUS_DISCONNECTED:
        qDebug() << "Endphone " << handler->handlerId();
        if(handler->getState() < VoiceCallHandler::StateCleanedUp) {
            handler->setState(VoiceCallHandler::StateCleanedUp);
            emit callEnded(qHash(handler->handlerId()), false);
        }
        break;
    case VoiceCallHandler::STATUS_ACTIVE:
        qDebug() << "Startphone" << handler->handlerId();
        if(handler->getState() < VoiceCallHandler::StateAnswered) {
            handler->setState(VoiceCallHandler::StateAnswered);
            emit callStarted(qHash(handler->handlerId()));
        }
        break;
    case VoiceCallHandler::STATUS_HELD:
        qDebug() << "OnHold" << handler->handlerId();
        break;
    }
}

void SailfishPlatform::onTimeChanged() {
    emit timeChanged();
}
struct AppID {
    QString type;
    QString sender;
    QString srcId;
};

AppID getAppID(watchfish::Notification *notification)
{
    QString owner = notification->originPackage().isEmpty()?notification->owner():notification->originPackage();
    if (owner.isEmpty()) owner = notification->sender();
    AppID ret;
    if (notification->owner() == "twitter-notifications-client") {
        ret.type="twitter";
        ret.srcId=notification->owner();
        ret.sender="Twitter";
    } else if (notification->category() == "x-nemo.email") {
        if (notification->sender().toLower().contains("gmail")) {
            ret.type="gmail";
            ret.sender="GMail";
            ret.srcId=notification->category()+"%3Agmail";
        } else {
            ret.type="email";
            ret.sender=notification->sender();
            ret.srcId=QString("mailto%3A")+notification->sender();
        }
    } else if (notification->owner() == "facebook-notifications-client") {
        ret.type="facebook";
        ret.srcId=notification->owner();
    } else if (notification->category() == "x-nemo.messaging.sms") {
        ret.type="sms";
        ret.sender="SMS";
        ret.srcId=notification->category();
    } else if (notification->originPackage() == "com.google.android.apps.babel" || notification->owner() == "harbour-hangish") {
        ret.type="hangouts";
        ret.srcId=owner;
    } else if (notification->category() == "x-nemo.messaging.im") {
        if(notification->actions().contains("default") && notification->actionArgs("default").at(0).toString().contains("gabble/jabber/google")) {
            ret.type="hangouts";
        } else {
            ret.type="sms";
        }
        ret.sender=notification->sender();
        ret.srcId=notification->category();
    } else if (notification->category() == "x-nemo.call.missed") {
        ret.type = "calls";
        ret.sender = notification->sender();
        ret.srcId = notification->category();
    } else if (notification->originPackage() == "org.telegram.messenger" || notification->category().startsWith("harbour.sailorgram")) {
        ret.type="telegram";
        ret.srcId=owner;
    } else if (notification->originPackage() == "com.whatsapp" || notification->owner().toLower().contains("whatsup")) {
        ret.type="whatsapp";
        ret.srcId=owner;
    } else {
        ret.type="generic";
        ret.sender=notification->sender();
        ret.srcId=owner;
    }
    return ret;
}

const QHash<QString,QStringList>& SailfishPlatform::cannedResponses() const
{
    return m_cans;
}
void SailfishPlatform::setCannedResponses(const QHash<QString, QStringList> &cans)
{
    // Accept only what we can handle, don't inject more than we have
    foreach(const QString &key, m_cans.keys()) {
        if(cans.contains(key))
            m_cans.insert(key,cans.value(key));
    }
}

void SailfishPlatform::newNotificationPin(watchfish::Notification *notification)
{
    qDebug() << "Got new notification from platform:" << notification->owner() << notification->summary();
    //HACK: ignore group notifications
    if (notification->category().endsWith(".group")) {
        qDebug() << "Skipping group notification.";
        return;
    }
    QJsonObject pin;
    QJsonObject layout;
    QJsonArray actions;

    AppID a = getAppID(notification);
    QStringList res = PlatformInterface::AppResMap.contains(a.type) ? PlatformInterface::AppResMap.value(a.type) : PlatformInterface::AppResMap.value("unknown");

    pin.insert("id",QString("%1.%2.%3").arg(a.sender).arg(notification->timestamp().toTime_t()).arg(notification->id()));
    QUuid guid = PlatformInterface::idToGuid(pin.value("id").toString());
    pin.insert("createTime", notification->timestamp().toUTC().toString(Qt::ISODate));
    pin.insert("guid",guid.toString().mid(1,36));
    pin.insert("type",QString("notification"));
    pin.insert("dataSource",QString("%1:%2").arg(a.srcId).arg(PlatformInterface::SysID));
    pin.insert("source",a.sender);
    if (!notification->icon().startsWith("/opt/alien/data/notificationIcon/")) //these are temporary, don't store them
        pin.insert("sourceIcon",notification->icon());
    if(res.count()>2 && !res.at(2).isEmpty()) {
        pin.insert("sourceName",res.at(2));
    }

    // Dismiss action is added implicitly by TimelinePin class
    if(m_cans.contains(a.srcId) && !m_cans.value(a.srcId).isEmpty()) {
        // We should have responses only for something we could do
        QJsonObject response;
        response.insert("type",QString("response"));
        response.insert("title",QString("Response"));
        response.insert("cannedResponse",QJsonArray::fromStringList(m_cans.value(a.srcId)));
        actions.append(response);
    }
    // Explicit open* will override implicit one
    foreach (const QString &actToken, notification->actions()) {
        if (actToken == "default") {
            qDebug() << "found action" << actToken;
            QJsonObject action;
            action.insert("type",QString("open:%1").arg(actToken));
            action.insert("title",QString("Open on Phone"));
            actions.append(action);
            // Sender is sent back as part of canned message response
            layout.insert("sender",actToken);
            break;
        }
    }
    pin.insert("actions",actions);

    layout.insert("type",QString("commNotification"));
    layout.insert("title",a.sender);
    layout.insert("subtitle",notification->summary());
    layout.insert("body",notification->body());

    layout.insert("tinyIcon",res.at(0));
    layout.insert("backgroundColor",res.at(1));

    pin.insert("layout",layout);

    connect(notification, &watchfish::Notification::closed, this, &SailfishPlatform::handleClosedNotification);
    m_notifs.insert(guid, notification); // keep for the action. TimelineManager will take care cleaning it up

    qDebug() << "Emitting new pin" << pin.value("id").toString() << pin.value("dataSource").toString() << pin.value("guid").toString();
    emit newTimelinePin(pin);
}

void SailfishPlatform::syncOrganizer(qint32 end) const
{
    m_organizerAdapter->reSync(end);
}
void SailfishPlatform::stopOrganizer() const
{
    m_organizerAdapter->disable();
}

void SailfishPlatform::sendTextMessage(const QString &contact, const QString &text) const
{
    qDebug() << "Sending text message for" << contact << text;
    telepathyResponse("/org/freedesktop/Telepathy/Account/ring/tel/account0",contact,text);
}

void SailfishPlatform::telepathyResponse(const QString &account, const QString &contact, const QString &text) const
{
    QDBusObjectPath acct(account);
    QVariantMap arg1,arg2;
    arg1.insert("message-type",0);
    arg2.insert("content-type",QString("text/plain"));
    arg2.insert("content",text);
    QDBusReply<QString> res = QDBusConnection::sessionBus().call(
                QDBusMessage::createMethodCall("org.freedesktop.Telepathy.ChannelDispatcher",
                                               "/org/freedesktop/Telepathy/ChannelDispatcher",
                                               "org.freedesktop.Telepathy.ChannelDispatcher.Interface.Messages.DRAFT", "SendMessage")
                << qVariantFromValue(acct) << contact << qVariantFromValue(QList<QVariantMap>({arg1,arg2})) << (quint32)0);
    if (res.isValid()) {
        if (res.value().isEmpty()) {
            qWarning() << "Unable to send response from" << account << "to" << contact << "with" << text;
        } else
            qDebug() << "Sent message under uuid" << res.value();
    } else {
        qWarning() << res.error().message();
    }
}

void SailfishPlatform::actionTriggered(const QUuid &uuid, const QString &actToken, const QJsonObject &param) const
{
    qDebug() << "Triggering notification" << uuid << "action" << actToken << QJsonDocument(param).toJson();
    watchfish::Notification *notif = m_notifs.value(uuid);
    if (notif) {
        if(actToken.split(":").first()=="open") {
            notif->invokeAction(actToken.split(":").last());
        } else if(actToken == "response") {
            if(param.contains("sender") && !notif->actionArgs(param.value("sender").toString()).isEmpty()
                    && notif->actionArgs(param.value("sender").toString()).at(0).toString().startsWith("/org/freedesktop/Telepathy/Account"))
                telepathyResponse(
                            notif->actionArgs(param.value("sender").toString()).at(0).toString(), // account dbus path
                            notif->actionArgs(param.value("sender").toString()).at(1).toString(), // destination contact
                            param.value("title").toString() // message text
                            );
            else
                qDebug() << "Don't know how to respond to" << notif->category() << notif->actionArgs(actToken);
        }
    } else
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

void SailfishPlatform::handleClosedNotification(watchfish::Notification::CloseReason reason) {
    watchfish::Notification *n = static_cast<watchfish::Notification*>(sender());
    qDebug() << "Notification closed:" << n->id() << "Reason: " << reason;
    disconnect(n, 0, this, 0);
    QMap<QUuid, watchfish::Notification*>::iterator it = m_notifs.begin();
    while (it != m_notifs.end()) {
        if (it.value()->id() == n->id()) {
            qDebug() << "Found notification to remove " << it.key();
            emit delTimelinePin(it.key().toString()); // Not sure we want it, but why not?
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
