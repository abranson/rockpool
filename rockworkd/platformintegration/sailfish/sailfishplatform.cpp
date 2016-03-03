#include "sailfishplatform.h"

#include "voicecallhandler.h"
#include "voicecallmanager.h"
#include "organizeradapter.h"
#include "syncmonitorclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <QSettings>

SailfishPlatform::SailfishPlatform(QObject *parent):
    PlatformInterface(parent),
    _pulseBus(NULL),
    _maxVolume(0)
{

    // Notifications
    QDBusConnection::sessionBus().registerObject("/org/freedesktop/Notifications", this, QDBusConnection::ExportAllSlots);
    m_iface = new QDBusInterface("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_iface->call("AddMatch", "type='method_call',interface='org.freedesktop.Notifications',member='Notify',eavesdrop='true'");
    m_iface->call("AddMatch", "type='method_return',sender='org.freedesktop.Notifications',eavesdrop='true'");
    m_iface->call("AddMatch", "type='signal',sender='org.freedesktop.Notifications',path='/org/freedesktop/Notifications',interface='org.freedesktop.Notifications',member='NotificationClosed'");

    // Music
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

    QDBusMessage call = QDBusMessage::createMethodCall("org.PulseAudio1", "/org/pulseaudio/server_lookup1", "org.freedesktop.DBus.Properties", "Get" );
    call << "org.PulseAudio.ServerLookup1" << "Address";
    QDBusReply<QDBusVariant> lookupReply = QDBusConnection::sessionBus().call(call);
    if (lookupReply.isValid()) {
        //
        qDebug() << "PulseAudio Bus address: " << lookupReply.value().variant().toString();
        _pulseBus = new QDBusConnection(QDBusConnection::connectToPeer(lookupReply.value().variant().toString(), "org.PulseAudio1"));
    }
    // Query max volume
    call = QDBusMessage::createMethodCall("com.Meego.MainVolume2", "/com/meego/mainvolume2",
                                                       "org.freedesktop.DBus.Properties", "Get");
    call << "com.Meego.MainVolume2" << "StepCount";
    QDBusReply<QDBusVariant> volumeMaxReply = _pulseBus->call(call);
    if (volumeMaxReply.isValid()) {
        _maxVolume = volumeMaxReply.value().variant().toUInt();
        qDebug() << "Max volume: " << _maxVolume;
    }
    else {
        qWarning() << "Could not read volume max, cannot adjust volume: " << volumeMaxReply.error().message();
    }

    // Calls
    m_voiceCallManager = new VoiceCallManager(this);

    connect(m_voiceCallManager, SIGNAL(activeVoiceCallChanged()), SLOT(onActiveVoiceCallChanged()));

    // Organizer
    m_organizerAdapter = new OrganizerAdapter(this);
    connect(m_organizerAdapter, &OrganizerAdapter::itemsChanged, this, &SailfishPlatform::organizerItemsChanged);
}

QDBusInterface *SailfishPlatform::interface() const
{
    return m_iface;
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

uint SailfishPlatform::Notify(const QString &app_name, uint replaces_id, const QString &app_icon, const QString &summary, const QString &body, const QStringList &actions, const QVariantHash &hints, int expire_timeout)
{
    qDebug() << "Notification received" << app_name << replaces_id << app_icon << summary << body << actions << hints << expire_timeout;
    QString owner = hints.value("x-nemo-owner", "").toString();

    // Look up the notification category and its parameters
    QString category = hints.value("category", "").toString();
    QHash<QString, QString> categoryParams = this->getCategoryParams(category);

    // Ignore transient and hidden notifications (notif hints override category hints)
    // Hack this to accept transient -preview and -summary notifications, as we don't know how to decode the actual notifs yet
    if (hints.value("transient", categoryParams.value("transient", "false")).toString() == "true") {
        qDebug() << "Ignoring transient notification from " << owner;
        return 0;
    }
    else if (hints.value("x-nemo-hidden", "false").toString() == "true" ) {
        qDebug() << "Ignoring hidden notification from " << owner;
        return 0;
    }

    Notification n(app_name);
    if (owner == "twitter-notifications-client") {
        n.setType(Notification::NotificationTypeTwitter);
        n.setSender("Twitter");
    } else if (category == "x-nemo.email") {
        if (app_name.toLower().contains("gmail")) {
            n.setType(Notification::NotificationTypeGMail);
            n.setSender("GMail");
        }
        else {
            n.setType(Notification::NotificationTypeEmail);
            n.setSender(app_name);
        }
    } else if (owner == "facebook-notifications-client") {
        n.setType(Notification::NotificationTypeFacebook);
        n.setSender("Facebook");
    } else if (hints.value("x-nemo-origin-package").toString() == "org.telegram.messenger"
               || category.startsWith("harbour.sailorgram")) {
        n.setType(Notification::NotificationTypeTelegram);
        n.setSender("Telegram");
    } else if (hints.value("x-nemo-origin-package").toString() == "com.google.android.apps.babel"
               || owner == "harbour-hangish") {
        n.setType(Notification::NotificationTypeHangout);
        n.setSender("Hangouts");
    } else if (hints.value("x-nemo-origin-package").toString() == "com.whatsapp"
               || owner.toLower().contains("whatsup")) {
        n.setType(Notification::NotificationTypeWhatsApp);
        n.setSender("Whatsapp");
    } else if (app_name.contains("indicator-datetime")) {
        n.setType(Notification::NotificationTypeReminder);
        n.setSender("reminders");
    } else {
        n.setType(Notification::NotificationTypeGeneric);
    }
    n.setSubject(summary);
    n.setBody(body);
    foreach (const QString &action, actions) {
        if (action == "default") {
            n.setActToken(hints.value("x-nemo-remote-action-default").toString());
            break;
        }
    }
    qDebug() << "have act token" << n.actToken();

    emit notificationReceived(n);
    // We never return something. We're just snooping in...
    setDelayedReply(true);
    return 0;
}

    QHash<QString, QString> SailfishPlatform::getCategoryParams(QString category)
    {
        if (!category.isEmpty()) {
            QString categoryConfigFile = QString("/usr/share/lipstick/notificationcategories/%1.conf").arg(category);
            QFile testFile(categoryConfigFile);
            if (testFile.exists()) {
                QHash<QString, QString> categories;
                QSettings settings(categoryConfigFile, QSettings::IniFormat);
                const QStringList settingKeys = settings.allKeys();
                foreach (const QString &settingKey, settingKeys) {
                    categories[settingKey] = settings.value(settingKey).toString();
                }
                return categories;
            }
        }
        return QHash<QString, QString>();
    }

void SailfishPlatform::sendMusicControlCommand(MusicControlButton controlButton)
{
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

    if (controlButton == MusicControlVolumeUp || controlButton == MusicControlVolumeDown) {
        QDBusMessage call = QDBusMessage::createMethodCall("com.Meego.MainVolume2", "/com/meego/mainvolume2",
                                                       "org.freedesktop.DBus.Properties", "Get");
        call << "com.Meego.MainVolume2" << "CurrentStep";

        QDBusReply<QDBusVariant> volumeReply = _pulseBus->call(call);
        if (volumeReply.isValid()) {
            // Decide the new value for volume, taking limits into account
            uint volume = volumeReply.value().variant().toUInt();
            uint newVolume;
            qDebug() << "Current volume: " << volumeReply.value().variant().toUInt();
            if (controlButton == MusicControlVolumeUp && volume < _maxVolume-1 ) {
                newVolume = volume + 1;
            }
            else if (controlButton == MusicControlVolumeDown && volume > 0) {
                newVolume = volume - 1;
            }
            else {
            qDebug() << "Volume already at limit";
            newVolume = volume;
            }

            // If we have a new volume level, change it
            if (newVolume != volume) {
                qDebug() << "Setting volume: " << newVolume;

                call = QDBusMessage::createMethodCall("com.Meego.MainVolume2", "/com/meego/mainvolume2",
                                                      "org.freedesktop.DBus.Properties", "Set");
                call << "com.Meego.MainVolume2" << "CurrentStep" << QVariant::fromValue(QDBusVariant(newVolume));

                QDBusError err = _pulseBus->call(call);
                if (err.isValid()) {
                    qWarning() << err.message();
                }
            }
        }
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

void SailfishPlatform::actionTriggered(const QString &actToken)
{
   QVariantMap action;
   // Extract the element of the DBus call
   QStringList elements(actToken.split(' ', QString::SkipEmptyParts));
   if (elements.size() <= 3) {
    qWarning() << "Unable to decode invalid remote action:" << actToken;
    } else {
    int index = 0;
    action.insert(QStringLiteral("service"), elements.at(index++));
    action.insert(QStringLiteral("path"), elements.at(index++));
    action.insert(QStringLiteral("iface"), elements.at(index++));
    action.insert(QStringLiteral("method"), elements.at(index++));

    if (index < elements.size()) {
        QVariantList args;
        while (index < elements.size()) {
            const QString &arg(elements.at(index++));
            const QByteArray buffer(QByteArray::fromBase64(arg.toUtf8()));

            QDataStream stream(buffer);
            QVariant var;
            stream >> var;
            args.append(var);
        }
      action.insert(QStringLiteral("arguments"), args);
    }
    qDebug() << "Calling: " << action;
    QDBusMessage call = QDBusMessage::createMethodCall(action.value("service").toString(), action.value("path").toString(), action.value("iface").toString(), action.value("method").toString());
    if (action.contains("arguments")) call.setArguments(action.value("arguments").toList());
    QDBusConnection::sessionBus().call(call);
   }
}

void SailfishPlatform::fetchMusicMetadata()
{
    if (!m_mprisService.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(m_mprisService, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get");
        call << "org.mpris.MediaPlayer2.Player" << "Metadata";
        QDBusPendingCall pcall = QDBusConnection::sessionBus().asyncCall(call);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pcall, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, &SailfishPlatform::fetchMusicMetadataFinished);
    }
}

void SailfishPlatform::fetchMusicMetadataFinished(QDBusPendingCallWatcher *watcher)
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

void SailfishPlatform::mediaPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidatedProps)
{
    Q_UNUSED(interface)
    Q_UNUSED(changedProps)
    Q_UNUSED(invalidatedProps)
    fetchMusicMetadata();
}
