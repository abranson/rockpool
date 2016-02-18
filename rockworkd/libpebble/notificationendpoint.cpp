#include "notificationendpoint.h"

#include "watchconnection.h"
#include "pebble.h"
#include "blobdb.h"

#include <QDebug>
#include <QDateTime>

NotificationEndpoint::NotificationEndpoint(Pebble *pebble, WatchConnection *watchConnection):
    QObject(pebble),
    m_pebble(pebble),
    m_watchConnection(watchConnection)
{
}

void NotificationEndpoint::sendLegacyNotification(const Notification &notification)
{
    LegacyNotification::Source source = LegacyNotification::SourceSMS;
    switch (notification.type()) {
    case Notification::NotificationTypeEmail:
        source = LegacyNotification::SourceEmail;
        break;
    case Notification::NotificationTypeFacebook:
        source = LegacyNotification::SourceFacebook;
        break;
    case Notification::NotificationTypeSMS:
        source = LegacyNotification::SourceSMS;
        break;
    case Notification::NotificationTypeTwitter:
        source = LegacyNotification::SourceTwitter;
        break;
    default:
        source = LegacyNotification::SourceSMS;
    }

    QString body = notification.subject().isEmpty() ? notification.body() : notification.subject();
    LegacyNotification legacyNotification(source, notification.sender(), body, QDateTime::currentDateTime(), notification.subject());
    m_watchConnection->writeToPebble(WatchConnection::EndpointNotification, legacyNotification.serialize());
}

void NotificationEndpoint::notificationReply(const QByteArray &data)
{
    qDebug() << "have notification reply" << data.toHex();

}
