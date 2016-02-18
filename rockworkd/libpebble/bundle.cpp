#include "bundle.h"

#include <QVariantMap>
#include <QFileInfo>
#include <QDebug>
#include <QJsonParseError>

Bundle::Bundle(const QString &path):
    m_path(path)
{

}

QString Bundle::path() const
{
    return m_path;
}

QString Bundle::file(Bundle::FileType type, HardwarePlatform hardwarePlatform) const
{
    // Those two will always be in the top level dir. HardwarePlatform is irrelevant.
    switch (type) {
    case FileTypeAppInfo:
        return m_path + "/appInfo.js";
    case FileTypeJsApp:
        return m_path + "/pebble-js-app.js";
    default:
        ;
    }

    // For all the others we have to find the manifest file
    QList<QString> possibleDirs;

    switch (hardwarePlatform) {
    case HardwarePlatformAplite:
        if (QFileInfo::exists(path() + "/aplite/")) {
            possibleDirs.append("aplite");
        }
        possibleDirs.append("");
        break;
    case HardwarePlatformBasalt:
        if (QFileInfo::exists(path() + "/basalt/")) {
            possibleDirs.append("basalt");
        }
        possibleDirs.append("");
        break;
    case HardwarePlatformChalk:
        if (QFileInfo::exists(path() + "/chalk/")) {
            possibleDirs.append("chalk");
        }
        break;
    default:
        possibleDirs.append("");
        ;
    }

    QString manifestFilename;
    QString subDir;
    foreach (const QString &dir, possibleDirs) {
        if (QFileInfo::exists(m_path + "/" + dir + "/manifest.json")) {
            subDir = "/" + dir;
            manifestFilename = m_path + subDir + "/manifest.json";
            break;
        }
    }
    if (manifestFilename.isEmpty()) {
        qWarning() << "Error finding manifest.json";
        return QString();
    }

    // We want the manifiest file. just return it without parsing it
    if (type == FileTypeManifest) {
        return manifestFilename;
    }

    QFile manifest(manifestFilename);
    if (!manifest.open(QFile::ReadOnly)) {
        qWarning() << "Error opening" << manifestFilename;
        return QString();
    }
    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(manifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Error parsing" << manifestFilename;
        return QString();
    }

    QVariantMap manifestMap = jsonDoc.toVariant().toMap();
    switch (type) {
    case FileTypeApplication:
        return m_path + subDir + "/" + manifestMap.value("application").toMap().value("name").toString();
    case FileTypeResources:
        if (manifestMap.contains("resources")) {
            return m_path + subDir + "/" + manifestMap.value("resources").toMap().value("name").toString();
        }
        break;
    case FileTypeWorker:
        if (manifestMap.contains("worker")) {
            return m_path + subDir + "/" + manifestMap.value("worker").toMap().value("name").toString();
        }
        break;
    case FileTypeFirmware:
        if (manifestMap.contains("firmware")) {
            return m_path + subDir + "/" + manifestMap.value("firmware").toMap().value("name").toString();
        }
        break;
    default:
        ;
    }
    return QString();
}

quint32 Bundle::crc(Bundle::FileType type, HardwarePlatform hardwarePlatform) const
{
    switch (type) {
    case FileTypeAppInfo:
    case FileTypeJsApp:
    case FileTypeManifest:
        qWarning() << "Cannot get crc for file type" << type;
        return 0;
    default: ;
    }

    QFile manifest(file(FileTypeManifest, hardwarePlatform));
    if (!manifest.open(QFile::ReadOnly)) {
        qWarning() << "Error opening manifest file";
        return 0;
    }

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(manifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Error parsing manifest file";
        return 0;
    }

    QVariantMap manifestMap = jsonDoc.toVariant().toMap();
    switch (type) {
    case FileTypeApplication:
        return manifestMap.value("application").toMap().value("crc").toUInt();
    case FileTypeResources:
        return manifestMap.value("resources").toMap().value("crc").toUInt();
    case FileTypeWorker:
        return manifestMap.value("worker").toMap().value("crc").toUInt();
    case FileTypeFirmware:
        return manifestMap.value("firmware").toMap().value("crc").toUInt();
    default:
        ;
    }
    return 0;
}
