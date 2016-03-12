#include "notification.h"

Notification::Notification(const QString &sourceId) :
    m_sourceId(sourceId)
{
    m_uuid = QUuid::createUuid();
}

QUuid Notification::uuid() const
{
    return m_uuid;
}

void Notification::setUuid(QUuid uuid)
{
    m_uuid = uuid;
}

QString Notification::sourceId() const
{
    return m_sourceId;
}

void Notification::setSourceId(const QString &sourceId)
{
    m_sourceId = sourceId;
}

QString Notification::sourceName() const
{
    return m_sourceName;
}

void Notification::setSourceName(const QString &sourceName)
{
    m_sourceName = sourceName;
}

QString Notification::sender() const
{
    return m_sender;
}

void Notification::setSender(const QString &sender)
{
    m_sender = sender;
}

QString Notification::subject() const
{
    return m_subject;
}

void Notification::setSubject(const QString &subject)
{
    m_subject = subject;
}

QString Notification::body() const
{
    return m_body;
}

void Notification::setBody(const QString &body)
{
    m_body = body;
}

Notification::NotificationType Notification::type() const
{
    return m_type;
}

void Notification::setType(Notification::NotificationType type)
{
    m_type = type;
}

QString Notification::actToken() const
{
    return m_actToken;
}

void Notification::setActToken(QString actToken)
{
    m_actToken = actToken;
}


