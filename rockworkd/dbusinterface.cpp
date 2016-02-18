#include "dbusinterface.h"
#include "core.h"
#include "pebblemanager.h"

DBusPebble::DBusPebble(Pebble *pebble, QObject *parent):
    QObject(parent),
    m_pebble(pebble)
{
    connect(pebble, &Pebble::pebbleConnected, this, &DBusPebble::Connected);
    connect(pebble, &Pebble::pebbleDisconnected, this, &DBusPebble::Disconnected);
    connect(pebble, &Pebble::installedAppsChanged, this, &DBusPebble::InstalledAppsChanged);
    connect(pebble, &Pebble::openURL, this, &DBusPebble::OpenURL);
    connect(pebble, &Pebble::notificationFilterChanged, this, &DBusPebble::NotificationFilterChanged);
    connect(pebble, &Pebble::screenshotAdded, this, &DBusPebble::ScreenshotAdded);
    connect(pebble, &Pebble::screenshotRemoved, this, &DBusPebble::ScreenshotRemoved);
    connect(pebble, &Pebble::updateAvailableChanged, this, &DBusPebble::FirmwareUpgradeAvailableChanged);
    connect(pebble, &Pebble::upgradingFirmwareChanged, this, &DBusPebble::UpgradingFirmwareChanged);
    connect(pebble, &Pebble::logsDumped, this, &DBusPebble::LogsDumped);
    connect(pebble, &Pebble::healtParamsChanged, this, &DBusPebble::HealthParamsChanged);
    connect(pebble, &Pebble::imperialUnitsChanged, this, &DBusPebble::ImperialUnitsChanged);
    connect(pebble, &Pebble::calendarSyncEnabledChanged, this, &DBusPebble::CalendarSyncEnabledChanged);
}

QString DBusPebble::Address() const
{
    return m_pebble->address().toString();
}

QString DBusPebble::Name() const
{
    return m_pebble->name();
}

bool DBusPebble::IsConnected() const
{
    return m_pebble->connected();
}

bool DBusPebble::Recovery() const
{
    return m_pebble->recovery();
}

bool DBusPebble::FirmwareUpgradeAvailable() const
{
    return m_pebble->firmwareUpdateAvailable();
}

QString DBusPebble::FirmwareReleaseNotes() const
{
    return m_pebble->firmwareReleaseNotes();
}

QString DBusPebble::CandidateFirmwareVersion() const
{
    return m_pebble->candidateFirmwareVersion();
}

QVariantMap DBusPebble::NotificationsFilter() const
{
    QVariantMap ret;
    QHash<QString, bool> filter = m_pebble->notificationsFilter();
    foreach (const QString &sourceId, filter.keys()) {
        ret.insert(sourceId, filter.value(sourceId));
    }
    return ret;
}

void DBusPebble::SetNotificationFilter(const QString &sourceId, bool enabled)
{
    m_pebble->setNotificationFilter(sourceId, enabled);
}

void DBusPebble::InstallApp(const QString &id)
{
    qDebug() << "installapp called" << id;
    m_pebble->installApp(id);
}

void DBusPebble::SideloadApp(const QString &packageFile)
{
    m_pebble->sideloadApp(packageFile);
}

QStringList DBusPebble::InstalledAppIds() const
{
    QStringList ret;
    foreach (const QUuid &id, m_pebble->installedAppIds()) {
        ret << id.toString();
    }
    return ret;
}

QVariantList DBusPebble::InstalledApps() const
{
    QVariantList list;
    foreach (const QUuid &appId, m_pebble->installedAppIds()) {
        QVariantMap app;
        AppInfo info = m_pebble->appInfo(appId);
        app.insert("storeId", info.storeId());
        app.insert("name", info.shortName());
        app.insert("vendor", info.companyName());
        app.insert("watchface", info.isWatchface());
        app.insert("version", info.versionLabel());
        app.insert("uuid", info.uuid().toString());
        app.insert("hasSettings", info.hasSettings());
        app.insert("icon", info.path() + "/list_image.png");
        app.insert("systemApp", info.isSystemApp());

        list.append(app);
    }
    return list;
}

void DBusPebble::RemoveApp(const QString &id)
{
    m_pebble->removeApp(id);
}

void DBusPebble::ConfigurationURL(const QString &uuid)
{
    m_pebble->requestConfigurationURL(QUuid(uuid));
}

void DBusPebble::ConfigurationClosed(const QString &uuid, const QString &result)
{
    m_pebble->configurationClosed(QUuid(uuid), result);
}

void DBusPebble::SetAppOrder(const QStringList &newList)
{
    QList<QUuid> uuidList;
    foreach (const QString &id, newList) {
        uuidList << QUuid(id);
    }
    m_pebble->setAppOrder(uuidList);
}

void DBusPebble::LaunchApp(const QString &uuid)
{
    m_pebble->launchApp(QUuid(uuid));
}

void DBusPebble::RequestScreenshot()
{
    m_pebble->requestScreenshot();
}

QStringList DBusPebble::Screenshots() const
{
    return m_pebble->screenshots();
}

void DBusPebble::RemoveScreenshot(const QString &filename)
{
    qDebug() << "Should remove screenshot" << filename;
    m_pebble->removeScreenshot(filename);
}

void DBusPebble::PerformFirmwareUpgrade()
{
    m_pebble->upgradeFirmware();
}

bool DBusPebble::UpgradingFirmware() const
{
    return m_pebble->upgradingFirmware();
}

QString DBusPebble::SerialNumber() const
{
    return m_pebble->serialNumber();
}

QString DBusPebble::HardwarePlatform() const
{
    switch (m_pebble->hardwarePlatform()) {
    case HardwarePlatformAplite:
        return "aplite";
    case HardwarePlatformBasalt:
        return "basalt";
    case HardwarePlatformChalk:
        return "chalk";
    default:
        ;
    }
    return "unknown";
}

QString DBusPebble::SoftwareVersion() const
{
    return m_pebble->softwareVersion();
}

int DBusPebble::Model() const
{
    return m_pebble->model();
}

void DBusPebble::DumpLogs(const QString &fileName) const
{
    qDebug() << "dumplogs" << fileName;
    m_pebble->dumpLogs(fileName);
}

QVariantMap DBusPebble::HealthParams() const
{
    QVariantMap map;
    map.insert("enabled", m_pebble->healthParams().enabled());
    map.insert("age", m_pebble->healthParams().age());
    map.insert("gender", m_pebble->healthParams().gender() == HealthParams::GenderFemale ? "female" : "male");
    map.insert("height", m_pebble->healthParams().height());
    map.insert("moreActive", m_pebble->healthParams().moreActive());
    map.insert("sleepMore", m_pebble->healthParams().sleepMore());
    map.insert("weight", m_pebble->healthParams().weight());
    return map;
}

void DBusPebble::SetHealthParams(const QVariantMap &healthParams)
{
    ::HealthParams params;
    params.setEnabled(healthParams.value("enabled").toBool());
    params.setAge(healthParams.value("age").toInt());
    params.setGender(healthParams.value("gender").toString() == "female" ? HealthParams::GenderFemale : HealthParams::GenderMale);
    params.setHeight(healthParams.value("height").toInt());
    params.setWeight(healthParams.value("weight").toInt());
    params.setMoreActive(healthParams.value("moreActive").toBool());
    params.setSleepMore(healthParams.value("sleepMore").toBool());
    m_pebble->setHealthParams(params);
}

bool DBusPebble::ImperialUnits() const
{
    return m_pebble->imperialUnits();
}

void DBusPebble::SetImperialUnits(bool imperialUnits)
{
    qDebug() << "setting imperial units" << imperialUnits;
    m_pebble->setImperialUnits(imperialUnits);
}

bool DBusPebble::CalendarSyncEnabled() const
{
    return m_pebble->calendarSyncEnabled();
}

void DBusPebble::SetCalendarSyncEnabled(bool enabled)
{
    m_pebble->setCalendarSyncEnabled(enabled);
}


DBusInterface::DBusInterface(QObject *parent) :
    QObject(parent)
{
    QDBusConnection::sessionBus().registerService("org.rockwork");
    QDBusConnection::sessionBus().registerObject("/org/rockwork/Manager", this, QDBusConnection::ExportScriptableSlots|QDBusConnection::ExportScriptableSignals);

    qDebug() << "pebble manager has:" << Core::instance()->pebbleManager()->pebbles().count() << Core::instance()->pebbleManager();
    foreach (Pebble *pebble, Core::instance()->pebbleManager()->pebbles()) {
        pebbleAdded(pebble);
    }

    qDebug() << "connecting dbus iface";
    connect(Core::instance()->pebbleManager(), &PebbleManager::pebbleAdded, this, &DBusInterface::pebbleAdded);
    connect(Core::instance()->pebbleManager(), &PebbleManager::pebbleRemoved, this, &DBusInterface::pebbleRemoved);
}

QString DBusInterface::Version()
{
    return QStringLiteral(VERSION);
}

QList<QDBusObjectPath> DBusInterface::ListWatches()
{
    QList<QDBusObjectPath> ret;
    foreach (const QString &address, m_dbusPebbles.keys()) {
        ret.append(QDBusObjectPath("/org/rockwork/" + address));
    }
    return ret;
}

void DBusInterface::pebbleAdded(Pebble *pebble)
{
    qDebug() << "pebble added";
    QString address = pebble->address().toString().replace(":", "_");
    if (m_dbusPebbles.contains(address)) {
        return;
    }

    qDebug() << "registering dbus iface";
    DBusPebble *dbusPebble = new DBusPebble(pebble, this);
    m_dbusPebbles.insert(address, dbusPebble);
    QDBusConnection::sessionBus().registerObject("/org/rockwork/" + address, dbusPebble, QDBusConnection::ExportAllContents);

    emit PebblesChanged();
}

void DBusInterface::pebbleRemoved(Pebble *pebble)
{
    QString address = pebble->address().toString().replace(":", "_");

    QDBusConnection::sessionBus().unregisterObject("/org/rockwork/" + address);
    m_dbusPebbles.remove(address);

    emit PebblesChanged();
}
