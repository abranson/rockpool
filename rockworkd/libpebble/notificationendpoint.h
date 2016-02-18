#include <QObject>
#include <QUuid>
#include <QDateTime>

#include "pebble.h"
#include "watchconnection.h"

class LegacyNotification: public PebblePacket
{
//    class Meta:
//        endpoint = 3000
//        endianness = '<'
public:
    enum Source {
        SourceEmail = 0,
        SourceSMS = 1,
        SourceFacebook = 2,
        SourceTwitter = 3
    };

    LegacyNotification(Source source, const QString &sender, const QString &body, const QDateTime &timestamp, const QString &subject):
        PebblePacket(),
        m_source(source),
        m_sender(sender),
        m_body(body),
        m_timestamp(timestamp),
        m_subject(subject)
    {}

    QByteArray serialize() const override
    {
        QByteArray ret;
        ret.append((quint8)m_source);
        ret.append(packString(m_sender));
        ret.append(packString(m_body));
        ret.append(packString(QString::number(m_timestamp.toMSecsSinceEpoch())));
        ret.append(packString(m_subject));
        return ret;
    }

private:

    Source m_source; // uint8
    QString m_sender;
    QString m_body;
    QDateTime m_timestamp;
    QString m_subject;
};

class NotificationEndpoint: public QObject
{
    Q_OBJECT
public:
    NotificationEndpoint(Pebble *pebble, WatchConnection *watchConnection);

    void sendLegacyNotification(const Notification &notification);

private slots:
    void notificationReply(const QByteArray &data);

private:
    Pebble *m_pebble;
    WatchConnection *m_watchConnection;
};
