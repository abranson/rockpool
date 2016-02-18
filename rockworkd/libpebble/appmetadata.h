#ifndef APPMETADATA_H
#define APPMETADATA_H

#include "watchconnection.h"

class AppMetadata: public PebblePacket
{
public:
    AppMetadata();

    QUuid uuid() const;
    void setUuid(const QUuid &uuid);
    void setFlags(quint32 flags);
    void setIcon(quint32 icon);
    void setAppVersion(quint8 appVersionMajor, quint8 appVersionMinor);
    void setSDKVersion(quint8 sdkVersionMajor, quint8 sdkVersionMinor);
    void setAppFaceBgColor(quint8 color);
    void setAppFaceTemplateId(quint8 templateId);
    void setAppName(const QString &appName);

    QByteArray serialize() const;
signals:

public slots:

private:
    QUuid m_uuid;
    quint32 m_flags;
    quint32 m_icon;
    quint8 m_appVersionMajor;
    quint8 m_appVersionMinor;
    quint8 m_sdkVersionMajor;
    quint8 m_sdkVersionMinor;
    quint8 m_appFaceBgColor;
    quint8 m_appFaceTemplateId;
    QString m_appName; // fixed, 96
};

#endif // APPMETADATA_H
