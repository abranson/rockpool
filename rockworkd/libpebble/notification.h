#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <QString>

class Notification
{
public:
    enum NotificationType {
        NotificationTypeGeneric,
        NotificationTypeEmail,
        NotificationTypeSMS,
        NotificationTypeFacebook,
        NotificationTypeTwitter,
        NotificationTypeTelegram,
        NotificationTypeWhatsApp,
        NotificationTypeHangout,
        NotificationTypeGMail,
        NotificationTypeWeather,
        NotificationTypeMusic,
        NotificationTypeMissedCall,
        NotificationTypeAlarm,
        NotificationTypeReminder,
    };

    Notification(const QString &sourceId = QString());

    QString sourceId() const;
    void setSourceId(const QString &sourceId);

    QString sourceName() const;
    void setSourceName(const QString &sourceName);

    QString sender() const;
    void setSender(const QString &sender);

    QString subject() const;
    void setSubject(const QString &subject);

    QString body() const;
    void setBody(const QString &body);

    NotificationType type() const;
    void setType(NotificationType type);

    QString actToken() const;
    void setActToken(QString actToken);

private:
    QString m_sourceId;
    QString m_sourceName;
    QString m_sender;
    QString m_subject;
    QString m_body;
    NotificationType m_type = NotificationTypeGeneric;
    QString m_actToken;
};

#endif // NOTIFICATION_H
