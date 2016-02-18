#include "appmetadata.h"

#include "watchdatawriter.h"

AppMetadata::AppMetadata()
{

}

QUuid AppMetadata::uuid() const
{
    return m_uuid;
}

void AppMetadata::setUuid(const QUuid &uuid)
{
    m_uuid = uuid;
}

void AppMetadata::setFlags(quint32 flags)
{
    m_flags = flags;
}

void AppMetadata::setIcon(quint32 icon)
{
    m_icon = icon;
}

void AppMetadata::setAppVersion(quint8 appVersionMajor, quint8 appVersionMinor)
{
    m_appVersionMajor = appVersionMajor;
    m_appVersionMinor = appVersionMinor;
}

void AppMetadata::setSDKVersion(quint8 sdkVersionMajor, quint8 sdkVersionMinor)
{
    m_sdkVersionMajor = sdkVersionMajor;
    m_sdkVersionMinor = sdkVersionMinor;
}

void AppMetadata::setAppFaceBgColor(quint8 color)
{
    m_appFaceBgColor = color;
}

void AppMetadata::setAppFaceTemplateId(quint8 templateId)
{
    m_appFaceTemplateId = templateId;
}

void AppMetadata::setAppName(const QString &appName)
{
    m_appName = appName;
}

QByteArray AppMetadata::serialize() const
{
    QByteArray ret;
    WatchDataWriter writer(&ret);
    writer.writeUuid(m_uuid);
    writer.writeLE<quint32>(m_flags);
    writer.writeLE<quint32>(m_icon);
    writer.writeLE<quint8>(m_appVersionMajor);
    writer.writeLE<quint8>(m_appVersionMinor);
    writer.writeLE<quint8>(m_sdkVersionMajor);
    writer.writeLE<quint8>(m_sdkVersionMinor);
    writer.writeLE<quint8>(m_appFaceBgColor);
    writer.writeLE<quint8>(m_appFaceTemplateId);
    writer.writeFixedString(96, m_appName);
    return ret;
}

