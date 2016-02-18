#ifndef BUNDLE_H
#define BUNDLE_H

#include <QString>

#include "enums.h"

class Bundle
{
public:
    enum FileType {
        FileTypeAppInfo,
        FileTypeJsApp,
        FileTypeManifest,
        FileTypeApplication,
        FileTypeResources,
        FileTypeWorker,
        FileTypeFirmware
    };

    Bundle(const QString &path = QString());

    QString path() const;

    QString file(FileType type, HardwarePlatform hardwarePlatform = HardwarePlatformUnknown) const;
    quint32 crc(FileType type, HardwarePlatform hardwarePlatform = HardwarePlatformUnknown) const;

private:
    QString m_path;

};

#endif // BUNDLE_H
