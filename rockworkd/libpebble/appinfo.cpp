#include <QSharedData>
#include <QBuffer>
#include <QDir>
#include <QJsonDocument>
#include <QUuid>
#include "appinfo.h"
#include "watchdatareader.h"
#include "pebble.h"

namespace {
struct ResourceEntry {
    int index;
    quint32 offset;
    quint32 length;
    quint32 crc;
};
}

AppInfo::AppInfo(const QString &path):
    Bundle(path)
{
    if (path.isEmpty()) {
        return;
    }

    QFile f(path + "/appinfo.json");
    if (!f.open(QFile::ReadOnly)) {
        qWarning() << "Error opening appinfo.json";
        return;
    }

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Error parsing appinfo.json";
        return;
    }

    m_storeId = path.split("/").last();

    QVariantMap map = jsonDoc.toVariant().toMap();

    m_uuid = map.value("uuid").toUuid();
    m_shortName = map.value("shortName").toString();
    m_longName = map.value("longName").toString();
    m_companyName = map.value("companyName").toString();
    m_versionCode = map.value("versionCode").toInt();
    m_versionLabel = map.value("versionLabel").toString();
    m_capabilities = 0;

    m_isWatchface = map.value("watchapp").toMap().value("watchface").toBool();

    if (map.contains("appKeys")) {
        QVariantMap appKeyMap = map.value("appKeys").toMap();
        foreach (const QString &key, appKeyMap.keys()) {
            m_appKeys.insert(key, appKeyMap.value(key).toInt());
        }
    }

    if (map.contains("capabilities")) {
        QList<QVariant> capabilities = map.value("capabilities").toList();

        foreach (const QVariant &value, capabilities) {
            QString capability = value.toString();
            if (capability == "location") {
                m_capabilities |= Location;
            }
            else if (capability == "configurable") {
                m_capabilities |= Configurable;
            }
        }
    }

    QFile jsApp(path + "/pebble-js-app.js");
    m_isJsKit = jsApp.exists();
}

AppInfo::AppInfo(const QUuid &uuid, bool isWatchFace, const QString &name, const QString &vendor, bool hasSettings):
    m_uuid(uuid),
    m_shortName(name),
    m_companyName(vendor),
    m_capabilities(hasSettings ? Configurable : None),
    m_isWatchface(isWatchFace),
    m_isSystemApp(true)
{

}


AppInfo::~AppInfo()
{}


bool AppInfo::isValid() const
{
    return !m_uuid.isNull();
}

QUuid AppInfo::uuid() const
{
    return m_uuid;
}

QString AppInfo::storeId() const
{
    return m_storeId;
}

QString AppInfo::shortName() const
{
    return m_shortName;
}

QString AppInfo::longName() const
{
    return m_longName;
}

QString AppInfo::companyName() const
{
    return m_companyName;
}

int AppInfo::versionCode() const
{
    return m_versionCode;
}

QString AppInfo::versionLabel() const
{
    return m_versionLabel;
}

bool AppInfo::isWatchface() const
{
    return m_isWatchface;
}

bool AppInfo::isJSKit() const
{
    return m_isJsKit;
}

bool AppInfo::isSystemApp() const
{
    return m_isSystemApp;
}

QHash<QString, int> AppInfo::appKeys() const
{
    return m_appKeys;
}

bool AppInfo::hasSettings() const
{
    return (m_capabilities & Configurable);
}

AppInfo::Capabilities AppInfo::capabilities() const
{
    return m_capabilities;
}

