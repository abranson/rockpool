#include "pebble.h"
#include "notificationsourcemodel.h"
#include "applicationsmodel.h"
#include "screenshotmodel.h"

#include <QDBusArgument>
#include <QDebug>

// TODO: Bootstrapping config from
// https://boot.getpebble.com/api/config/android/v3/1055?locale=de_DE&app_version=3.13.0-1055-06644a6
Pebble::Pebble(const QDBusObjectPath &path, QObject *parent):
    QObject(parent),
    m_path(path)
{
    m_iface = new QDBusInterface("org.rockwork", path.path(), "org.rockwork.Pebble", QDBusConnection::sessionBus(), this);
    m_notifications = new NotificationSourceModel(this);
    m_installedApps = new ApplicationsModel(this);
    connect(m_installedApps, &ApplicationsModel::appsSorted, this, &Pebble::appsSorted);
    m_installedWatchfaces = new ApplicationsModel(this);
    connect(m_installedWatchfaces, &ApplicationsModel::appsSorted, this, &Pebble::appsSorted);
    m_screenshotModel = new ScreenshotModel(this);

    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "Connected", this, SLOT(pebbleConnected()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "Disconnected", this, SLOT(pebbleDisconnected()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "InstalledAppsChanged", this, SLOT(refreshApps()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "OpenURL", this, SIGNAL(openURL(const QString&, const QString&)));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "NotificationFilterChanged", this, SLOT(notificationFilterChanged(const QString &, const QString &, const QString &, const int )));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "ScreenshotAdded", this, SLOT(screenshotAdded(const QString &)));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "ScreenshotRemoved", this, SLOT(screenshotRemoved(const QString &)));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "FirmwareUpgradeAvailableChanged", this, SLOT(refreshFirmwareUpdateInfo()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "LanguageVersionChanged", this, SIGNAL(languageVersionChanged()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "UpgradingFirmwareChanged", this, SIGNAL(refreshFirmwareUpdateInfo()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "LogsDumped", this, SIGNAL(logsDumped(bool)));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "HealthParamsChanged", this, SIGNAL(healthParamsChanged()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "ImperialUnitsChanged", this, SIGNAL(imperialUnitsChanged()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "ProfileWhenConnectedChanged", this, SIGNAL(profileWhenConnectedChanged()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "ProfileWhenDisconnectedChanged", this, SIGNAL(profileWhenDisconnectedChanged()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "CalendarSyncEnabledChanged", this, SIGNAL(calendarSyncEnabledChanged()));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "DevConnectionChanged", this, SLOT(devConStateChanged(bool)));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "DevConnCloudChanged", this, SLOT(devConCloudChanged(bool)));
    QDBusConnection::sessionBus().connect("org.rockwork", path.path(), "org.rockwork.Pebble", "oauthTokenChanged", this, SLOT(setOAuthToken(QString)));

    dataChanged();
    refreshApps();
    refreshNotifications();
    refreshScreenshots();
    refreshFirmwareUpdateInfo();
}

bool Pebble::connected() const
{
    return m_connected;
}

QDBusObjectPath Pebble::path()
{
    return m_path;
}

QString Pebble::address() const
{
    return m_address;
}

QString Pebble::name() const
{
    return m_name;
}

QString Pebble::platformString() const
{
    return fetchProperty("PlatformString").toString();
}

QString Pebble::hardwarePlatform() const
{
    return m_hardwarePlatform;
}

QString Pebble::serialNumber() const
{
    return m_serialNumber;
}

QString Pebble::softwareVersion() const
{
    return m_softwareVersion;
}

QString Pebble::languageVersion() const
{
    return fetchProperty("LanguageVersion").toString();
}

void Pebble::loadLanguagePack(const QString &pblFile)
{
    qDebug() << "Requesting to load language from" << pblFile;
    m_iface->call("LoadLanguagePack", pblFile);
}

int Pebble::model() const
{
    return m_model;
}

bool Pebble::recovery() const
{
    return m_recovery;
}

bool Pebble::upgradingFirmware() const
{
    qDebug() << "upgrading firmware" << m_upgradingFirmware;
    return m_upgradingFirmware;
}

NotificationSourceModel *Pebble::notifications() const
{
    return m_notifications;
}

ApplicationsModel *Pebble::installedApps() const
{
    return m_installedApps;
}

ApplicationsModel *Pebble::installedWatchfaces() const
{
    return m_installedWatchfaces;
}

ScreenshotModel *Pebble::screenshots() const
{
    return m_screenshotModel;
}

bool Pebble::firmwareUpgradeAvailable() const
{
    return m_firmwareUpgradeAvailable;
}

QString Pebble::firmwareReleaseNotes() const
{
    return m_firmwareReleaseNotes;
}

QString Pebble::candidateVersion() const
{
    return m_candidateVersion;
}

QVariantMap Pebble::fetchVarMap(const QString &propertyName, const QStringList *keys) const
{
    QDBusMessage m = ((keys) ? m_iface->call(propertyName, *keys) : m_iface->call(propertyName));
    if (m.type() == QDBusMessage::ErrorMessage || m.arguments().count() == 0) {
        qWarning() << "Could not fetch" << propertyName << m.errorMessage();
        return QVariantMap();
    }

    const QDBusArgument &arg = m.arguments().first().value<QDBusArgument>();

    QVariantMap mapEntryVariant;
    arg >> mapEntryVariant;

    qDebug() << "have" << propertyName << mapEntryVariant;
    return mapEntryVariant;
}

void Pebble::sendVarMap(const QString &property, const QVariantMap &values)
{
    QVariantMap vals;
    foreach(const QString &key, values.keys()) {
        QStringList msgs;
        if(values.value(key).type()==QVariant::StringList) {
            msgs = values.value(key).toStringList();
        } else if(values.value(key).type()==QVariant::Map) {
            foreach(const QVariant &msg,values.value(key).toMap().values()) {
                msgs.append(msg.toString());
            }
        } else if(values.value(key).type()==QVariant::List) {
            msgs = values.value(key).toStringList();
        }  else {
            qWarning() << "Cannot convert to StringList" << values.value(key);
        }
        qDebug() << "Adding" << key << values.value(key) << msgs;
        if(!msgs.isEmpty())
            vals.insert(key,msgs);
    }
    qDebug() << "Setting Map of StringLists" << vals;
    m_iface->call(property, vals);
}

QVariantMap Pebble::cannedResponses() const
{
    return fetchVarMap("cannedResponses");
}
void Pebble::setCannedResponses(const QVariantMap &cans)
{
    sendVarMap("setCannedResponses",cans);
    emit cannedResponsesChanged();
}
QVariantMap Pebble::getCannedResponses(const QStringList &keys)
{
    return fetchVarMap("getCannedResponses",&keys);
}
void Pebble::setCannedContacts(const QVariantMap &cans)
{
    sendVarMap("setFavoriteContacts",cans);
}
QVariantMap Pebble::getCannedContacts(const QStringList &keys)
{
    return fetchVarMap("getFavoriteContacts",&keys);
}

QVariantList Pebble::weatherLocations() const
{
    QVariantList retList;
    QDBusMessage m = m_iface->call("WeatherLocations");
    if (m.type() == QDBusMessage::ErrorMessage || m.arguments().count() == 0) {
        qWarning() << "Could not fetch installed apps" << m.errorMessage();
        return retList;
    }
    const QDBusArgument &arg = m.arguments().first().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        QVariant entryVariant;
        arg >> entryVariant;
        retList.append(entryVariant);
    }
    arg.endArray();
    qDebug() << retList;
    return retList;
}
void Pebble::setWeatherLocations(const QVariantList &in)
{
    qDebug() << in;
    QVariantList out;
    foreach(const QVariant &v,in) {
        if(v.canConvert(QVariant::StringList))
            out.append(v.toStringList());
        else if(v.canConvert(QVariant::List)) {
            QStringList l;
            foreach(const QVariant &sv,v.toList()) {
                l.append(sv.toString());
            }
            out.append(l);
        }
    }
    qDebug() << out;
    m_iface->call("SetWeatherLocations",out);
    emit weatherLocationsChanged();
}

QString Pebble::weatherUnits() const
{
    return fetchProperty("WeatherUnits").toString();
}
void Pebble::setWeatherUnits(const QString &u)
{
    m_iface->call("setWeatherUnits",u);
    emit weatherUnitsChanged();
}

QString Pebble::weatherLanguage() const
{
    return fetchProperty("WeatherLanguage").toString();
}
void Pebble::setWeatherLanguage(const QString &l)
{
    m_iface->call("setWeatherLanguage",l);
    emit weatherLanguageChanged();
}

QString Pebble::weatherAltKey() const
{
    return fetchProperty("WeatherAltKey").toString();
}
void Pebble::setWeatherAltKey(const QString &key)
{
    m_iface->call("setWeatherAltKey",key);
    emit weatherAltKeyChanged();
}

QVariantMap Pebble::healthParams() const
{
    return fetchVarMap("HealthParams");
}

void Pebble::setHealthParams(const QVariantMap &healthParams)
{
    m_iface->call("SetHealthParams", healthParams);
}

bool Pebble::imperialUnits() const
{
    return fetchProperty("ImperialUnits").toBool();
}

void Pebble::setImperialUnits(bool imperialUnits)
{
    qDebug() << "setting im units" << imperialUnits;
    m_iface->call("SetImperialUnits", imperialUnits);
}

QString Pebble::profileWhenConnected()
{
    return fetchProperty("ProfileWhenConnected").toString();
}

QString Pebble::profileWhenDisconnected()
{
    return fetchProperty("ProfileWhenDisconnected").toString();
}

void Pebble::setProfileWhenConnected(const QString &profile)
{
    qDebug() << "setting profile when connected: " << profile;
    m_iface->call("SetProfileWhenConnected", profile);
}

void Pebble::setProfileWhenDisconnected(const QString &profile)
{
    qDebug() << "setting profile when disconnected: " << profile;
    m_iface->call("SetProfileWhenDisconnected", profile);
}

bool Pebble::calendarSyncEnabled() const
{
    return fetchProperty("CalendarSyncEnabled").toBool();
}

void Pebble::setCalendarSyncEnabled(bool enabled)
{
    m_iface->call("SetCalendarSyncEnabled", enabled);
}

bool Pebble::devConnEnabled() const
{
    return fetchProperty("DevConnectionEnabled").toBool();
}
void Pebble::setDevConnEnabled(bool enabled)
{
    m_iface->call("SetDevConnEnabled", enabled);
}

bool Pebble::devConnCloudEnabled() const
{
    return fetchProperty("DevConnCloudEnabled").toBool();
}
void Pebble::setDevConnCloudEnabled(bool enabled)
{
    m_iface->call("SetDevConnCloudEnabled",enabled);
}

quint16 Pebble::devConListenPort() const
{
    return (quint16)fetchProperty("DevConnListenPort").toInt();
}
void Pebble::setDevConListenPort(quint16 port)
{
    m_iface->call("SetDevConnListenPort",port);
}

bool Pebble::devConnServerRunning() const
{
    return fetchProperty("DevConnectionState").toBool();
}

bool Pebble::devConCloudConnected() const
{
    return fetchProperty("DevConnCloudState").toBool();
}

void Pebble::devConStateChanged(bool state)
{
    qDebug() << "DevCon state hase changed:" << (state?"running":"stopped");
    emit devConnServerRunningChanged();
}

void Pebble::devConCloudChanged(bool state)
{
    qDebug() << "DevConCloud state changed:" << (state?"connected":"disconnected");
    emit devConCloudConnectedChanged();
}

void Pebble::setLogLevel(int level)
{
    m_iface->call("setLogLevel",level);
    emit logLevelChanged();
}
int Pebble::getLogLevel() const
{
    return fetchProperty("getLogLevel").toInt();
}

QString Pebble::getLogDump()
{
    return fetchProperty("getLogDump").toString();
}
QString Pebble::startLogDump()
{
    QString ret = fetchProperty("startLogDump").toString();
    emit logDumpChanged();
    return ret;
}
QString Pebble::stopLogDump()
{
    QString ret = fetchProperty("stopLogDump").toString();
    emit logDumpChanged();
    return ret;
}
bool Pebble::isLogDumping()
{
    return fetchProperty("isLogDumping").toBool();
}

QString Pebble::oauthToken() const
{
    return fetchProperty("oauthToken").toString();
}

void Pebble::setOAuthToken(const QString &token)
{
    m_iface->call("setOAuthToken",token);
    emit oauthTokenChanged();
    emit accountNameChanged();
    emit accountEmailChanged();
}

QString Pebble::accountName() const
{
    return fetchProperty("accountName").toString();
}

QString Pebble::accountEmail() const
{
    return fetchProperty("accountEmail").toString();
}

bool Pebble::syncAppsFromCloud() const
{
    return fetchProperty("syncAppsFromCloud").toBool();
}
void Pebble::setSyncAppsFromCloud(bool enable)
{
    m_iface->call("setSyncAppsFromCloud",enable);
    emit syncAppsFromCloudChanged();
}

void Pebble::resetTimeline()
{
    m_iface->call("resetTimeline");
}

void Pebble::setTimelineWindow()
{
    m_iface->call("setTimelineWindow",-m_timelienWindowStart,-m_timelienWindowFade,m_timelienWindowEnd);
}

void Pebble::configurationClosed(const QString &uuid, const QString &url)
{
    m_iface->call("ConfigurationClosed", uuid, url);
}

void Pebble::launchApp(const QString &uuid)
{
    m_iface->call("LaunchApp", uuid);
}

void Pebble::requestConfigurationURL(const QString &uuid)
{
    m_iface->call("ConfigurationURL", uuid);
}

void Pebble::removeApp(const QString &uuid)
{
    qDebug() << "should remove app" << uuid;
    m_iface->call("RemoveApp", uuid);
}

void Pebble::installApp(const QString &storeId)
{
    qDebug() << "should install app" << storeId;
    m_iface->call("InstallApp", storeId);
}

void Pebble::sideloadApp(const QString &packageFile)
{
    m_iface->call("SideloadApp", packageFile);
}

QVariant Pebble::fetchProperty(const QString &propertyName) const
{
    QDBusMessage m = m_iface->call(propertyName);
    if (m.type() != QDBusMessage::ErrorMessage && m.arguments().count() == 1) {
        qDebug() << "property" << propertyName << m.arguments().first();
        return m.arguments().first();

    }
    qDebug() << "error getting property:" << propertyName << m.errorMessage();
    return QVariant();
}

void Pebble::dataChanged()
{
    qDebug() << "data changed";
    m_name = fetchProperty("Name").toString();
    m_address = fetchProperty("Address").toString();
    m_serialNumber = fetchProperty("SerialNumber").toString();
    m_serialNumber = fetchProperty("SerialNumber").toString();
    QString hardwarePlatform = fetchProperty("HardwarePlatform").toString();
    if (hardwarePlatform != m_hardwarePlatform) {
        m_hardwarePlatform = hardwarePlatform;
        emit hardwarePlatformChanged();
    }
    m_softwareVersion = fetchProperty("SoftwareVersion").toString();
    m_model = fetchProperty("Model").toInt();
    m_recovery = fetchProperty("Recovery").toBool();
    qDebug() << "model is" << m_model;
    emit modelChanged();

    bool connected = fetchProperty("IsConnected").toBool();
    if (connected != m_connected) {
        m_connected = connected;
        emit connectedChanged();
    }
    m_timelienWindowStart = -fetchProperty("timelineWindowStart").toInt();
    m_timelienWindowFade = -fetchProperty("timelineWindowFade").toInt();
    m_timelienWindowEnd = fetchProperty("timelineWindowEnd").toInt();
}

void Pebble::pebbleConnected()
{

    dataChanged();
    m_connected = true;
    emit connectedChanged();

    refreshApps();
    refreshNotifications();
    refreshScreenshots();
}

void Pebble::pebbleDisconnected()
{
    m_connected = false;
    emit connectedChanged();
}

void Pebble::notificationFilterChanged(const QString &sourceId, const QString &name, const QString &icon, const int enabled)
{
    m_notifications->insert(sourceId, name, icon, enabled);
    emit notificationsFilterChanged();
}

QVariantMap Pebble::notificationsFilter() const
{
    QVariantMap mapEntryVariant = fetchVarMap("NotificationsFilter");
    QVariantMap ret;

    foreach (const QString &sourceId, mapEntryVariant.keys()) {
        const QDBusArgument &arg2 = qvariant_cast<QDBusArgument>(mapEntryVariant.value(sourceId));
        QVariantMap notifEntry;
        arg2 >> notifEntry;
        ret.insert(sourceId, notifEntry);
    }
    return ret;
}

void Pebble::refreshNotifications()
{
    QVariantMap mapEntryVariant = fetchVarMap("NotificationsFilter");

    foreach (const QString &sourceId, mapEntryVariant.keys()) {
        const QDBusArgument &arg2 = qvariant_cast<QDBusArgument>(mapEntryVariant.value(sourceId));
        QVariantMap notifEntry;
        arg2 >> notifEntry;
        m_notifications->insert(sourceId, notifEntry.value("name").toString(), notifEntry.value("icon").toString(), notifEntry.value("enabled").toInt());
    }
}

void Pebble::setNotificationFilter(const QString &sourceId, int enabled)
{
    m_iface->call("SetNotificationFilter", sourceId, enabled);
    emit notificationsFilterChanged();
}

void Pebble::forgetNotificationFilter(const QString &sourceId)
{
    m_iface->call("ForgetNotificationFilter", sourceId);
    emit notificationsFilterChanged();
}

void Pebble::refreshApps()
{
    QDBusMessage m = m_iface->call("InstalledApps");
    if (m.type() == QDBusMessage::ErrorMessage || m.arguments().count() == 0) {
        qWarning() << "Could not fetch installed apps" << m.errorMessage();
        return;
    }

    m_installedApps->clear();
    m_installedWatchfaces->clear();

    const QDBusArgument &arg = m.arguments().first().value<QDBusArgument>();

    QVariantList appList;

    arg.beginArray();
    while (!arg.atEnd()) {
        QVariant mapEntryVariant;
        arg >> mapEntryVariant;

        QDBusArgument mapEntry = mapEntryVariant.value<QDBusArgument>();
        QVariantMap appMap;
        mapEntry >> appMap;
        appList.append(appMap);

    }
    arg.endArray();


    qDebug() << "have apps" << appList;
    foreach (const QVariant &v, appList) {
        AppItem *app = new AppItem(this);
        app->setStoreId(v.toMap().value("storeId").toString());
        app->setUuid(v.toMap().value("uuid").toString());
        app->setName(v.toMap().value("name").toString());
        app->setIcon(v.toMap().value("icon").toString());
        app->setVendor(v.toMap().value("vendor").toString());
        app->setVersion(v.toMap().value("version").toString());
        app->setIsWatchFace(v.toMap().value("watchface").toBool());
        app->setHasSettings(v.toMap().value("hasSettings").toBool());
        app->setIsSystemApp(v.toMap().value("systemApp").toBool());

        if (app->isWatchFace()) {
            m_installedWatchfaces->insert(app);
        } else {
            m_installedApps->insert(app);
        }
    }
}

void Pebble::appsSorted()
{
    QStringList newList;
    for (int i = 0; i < m_installedApps->rowCount(); i++) {
        newList << m_installedApps->get(i)->uuid();
    }
    for (int i = 0; i < m_installedWatchfaces->rowCount(); i++) {
        newList << m_installedWatchfaces->get(i)->uuid();
    }
    m_iface->call("SetAppOrder", newList);
}

void Pebble::refreshScreenshots()
{
    m_screenshotModel->clear();
    QStringList screenshots = fetchProperty("Screenshots").toStringList();
    foreach (const QString &filename, screenshots) {
        m_screenshotModel->insert(filename);
    }
}

void Pebble::screenshotAdded(const QString &filename)
{
    qDebug() << "screenshot added" << filename;
    m_screenshotModel->insert(filename);
}

void Pebble::screenshotRemoved(const QString &filename)
{
    m_screenshotModel->remove(filename);
}

void Pebble::refreshFirmwareUpdateInfo()
{
    bool firmwareUpgradeAvailable = fetchProperty("FirmwareUpgradeAvailable").toBool();
    if (firmwareUpgradeAvailable && !m_firmwareUpgradeAvailable) {
        m_firmwareUpgradeAvailable = true;
        m_firmwareReleaseNotes = fetchProperty("FirmwareReleaseNotes").toString();
        m_candidateVersion = fetchProperty("CandidateFirmwareVersion").toString();
        qDebug() << "firmare upgrade" << m_firmwareUpgradeAvailable << m_firmwareReleaseNotes << m_candidateVersion;
        emit firmwareUpgradeAvailableChanged();
    } else if (!firmwareUpgradeAvailable && m_firmwareUpgradeAvailable) {
        m_firmwareUpgradeAvailable = false;
        m_firmwareReleaseNotes.clear();;
        m_candidateVersion.clear();
        emit firmwareUpgradeAvailableChanged();
    }
    bool upgradingFirmware = fetchProperty("UpgradingFirmware").toBool();
    if (m_upgradingFirmware != upgradingFirmware) {
        m_upgradingFirmware = upgradingFirmware;
        emit upgradingFirmwareChanged();
    }
}

void Pebble::requestScreenshot()
{
    m_iface->call("RequestScreenshot");
}

void Pebble::removeScreenshot(const QString &filename)
{
    qDebug() << "removing screenshot" << filename;
    m_iface->call("RemoveScreenshot", filename);
}

void Pebble::performFirmwareUpgrade()
{
    m_iface->call("PerformFirmwareUpgrade");
}

void Pebble::dumpLogs(const QString &filename)
{
    m_iface->call("DumpLogs", filename);
}
