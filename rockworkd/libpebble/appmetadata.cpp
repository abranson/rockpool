#include "appmetadata.h"

#include "watchdatawriter.h"
#include "watchdatareader.h"
#include "appinfo.h"

AppMetadata::AppMetadata()
{
}

AppMetadata::AppMetadata(const AppInfo &info, int hardwarePlatform):
    m_appFaceBgColor(0),
    m_appFaceTemplateId(0)
{
    QString binaryFile = info.file(AppInfo::FileTypeApplication, (HardwarePlatform)hardwarePlatform);
    QFile f(binaryFile);
    if (f.open(QFile::ReadOnly)) {
        QByteArray data = f.read(512);
        WatchDataReader reader(data);
        qDebug() << "Header:" << reader.readFixedString(8);
        qDebug() << "struct Major version:" << reader.read<quint8>();
        qDebug() << "struct Minor version:" << reader.read<quint8>();
        m_sdkVersionMajor = reader.read<quint8>();
        qDebug() << "sdk Major version:" << m_sdkVersionMajor;
        m_sdkVersionMinor = reader.read<quint8>();
        qDebug() << "sdk Minor version:" << m_sdkVersionMinor;
        m_appVersionMajor = reader.read<quint8>();
        qDebug() << "app Major version:" << m_appVersionMajor;
        m_appVersionMinor = reader.read<quint8>();
        qDebug() << "app Minor version:" << m_appVersionMinor;
        qDebug() << "size:" << reader.readLE<quint16>();
        qDebug() << "offset:" << reader.readLE<quint32>();
        qDebug() << "crc:" << reader.readLE<quint32>();
        m_appName = reader.readFixedString(32);
        qDebug() << "App name:" << m_appName;
        qDebug() << "Vendor name:" << reader.readFixedString(32);
        m_icon = reader.readLE<quint32>();
        qDebug() << "Icon:" << m_icon;
        qDebug() << "Symbol table address:" << reader.readLE<quint32>();
        m_flags = reader.readLE<quint32>();
        qDebug() << "Flags:" << m_flags;
        qDebug() << "Num relocatable entries:" << reader.readLE<quint32>();

        f.close();
        qDebug() << "app data" << data.toHex();

        m_uuid = info.uuid();
    } else {
        qWarning() << "Error opening app binary";
    }
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

QByteArray AppMetadata::itemKey() const
{
    return m_uuid.toRfc4122();
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
