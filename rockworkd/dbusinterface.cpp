#include "dbusinterface.h"
#include "core.h"
#include "pebblemanager.h"
#include "libpebble/devconnection.h"

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
    connect(pebble, &Pebble::languagePackChanged, this, &DBusPebble::LanguageVersionChanged);
    connect(pebble, &Pebble::logsDumped, this, &DBusPebble::LogsDumped);
    connect(pebble, &Pebble::healtParamsChanged, this, &DBusPebble::HealthParamsChanged);
    connect(pebble, &Pebble::imperialUnitsChanged, this, &DBusPebble::ImperialUnitsChanged);
    connect(pebble, &Pebble::profileConnectionSwitchChanged, this, &DBusPebble::onProfileConnectionSwitchChanged);
    connect(pebble, &Pebble::calendarSyncEnabledChanged, this, &DBusPebble::CalendarSyncEnabledChanged);
    connect(pebble, &Pebble::weatherLocationsChanged, this, &DBusPebble::WeatherLocationsChanged);
    connect(pebble, &Pebble::devConServerStateChanged, this, &DBusPebble::DevConnectionChanged);
    connect(pebble, &Pebble::devConCloudStateChanged, this, &DBusPebble::DevConnCloudChanged);
    connect(pebble, &Pebble::oauthTokenChanged, this, &DBusPebble::oauthTokenChanged);
    connect(pebble, &Pebble::voiceSessionSetup, this, &DBusPebble::voiceSessionSetup);
    connect(pebble, &Pebble::voiceSessionStream, this, &DBusPebble::voiceSessionStream);
    connect(pebble, &Pebble::voiceSessionDumped, this, &DBusPebble::voiceSessionDumped);
    connect(pebble, &Pebble::voiceSessionClosed, this, &DBusPebble::voiceSessionClosed);
    connect(pebble, &Pebble::appButtonPressed, this, &DBusPebble::AppButtonPressed);
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
    return m_pebble->notificationsFilter();
}

void DBusPebble::SetNotificationFilter(const QString &sourceId, int enabled)
{
    m_pebble->setNotificationFilter(sourceId, Pebble::NotificationFilter(enabled));
}

void DBusPebble::ForgetNotificationFilter(const QString &sourceId)
{
    m_pebble->forgetNotificationFilter(sourceId);
}

QVariantMap DBusPebble::cannedResponses() const
{
    return m_pebble->cannedMessages();
}
/**
 * @brief DBusPebble::setCannedResponses
 * @param cans aass
 * @example gdbus call -e -d org.rockwork -o /org/rockwork/XX_XX_XX_XX_XX_XX -m org.rockwork.Pebble.setCannedResponses \
 *  "{'x-nemo.messaging.im': <['Aye','Nay','On my way']>, 'x-nemo.messaging.sms': <['Ok']>, 'com.pebble.sendText':<["Where are you?"]>}"
 */
void DBusPebble::setCannedResponses(const QVariantMap &cans)
{
    m_pebble->setCannedMessages(cans);
}
QVariantMap DBusPebble::getCannedResponses(const QStringList &groups) const
{
    QHash<QString,QStringList> cans = m_pebble->getCannedMessages(groups);
    QVariantMap ret;
    foreach(const QString &key,cans.keys()) {
        ret.insert(key,QVariant::fromValue(cans.value(key)));
    }
    return ret;
}

/**
 * @brief DBusPebble::setFavoriteContacts
 * @param cans
 * @example gdbus call -e -d org.rockwork -o /org/rockwork/B0_B4_48_00_00_00 -m org.rockwork.Pebble.setFavoriteContacts \
 * '{"Wife":<["+420987654321","+420123456789"]>,"Myself":<["+420555322223"]>}'
 */
void DBusPebble::setFavoriteContacts(const QVariantMap &cans)
{
    QMap<QString,QStringList> ctxs;
    foreach(const QString &key, cans.keys()) {
        ctxs.insert(key,cans.value(key).toStringList());
    }
    m_pebble->setCannedContacts(ctxs);
}
QVariantMap DBusPebble::getFavoriteContacts(const QStringList &names) const
{
    QMap<QString,QStringList> cans = m_pebble->getCannedContacts(names);
    QVariantMap ret;
    foreach(const QString &key,cans.keys()) {
        ret.insert(key,QVariant::fromValue(cans.value(key)));
    }
    return ret;
}

/**
 * @brief DBusPebble::voiceSessionResult
 * @param s dumpFile
 * @param aaasu sentences
 */
void DBusPebble::voiceSessionResult(const QString &dumpFile, const QVariantList &sentences)
{
    m_pebble->voiceSessionResult(dumpFile, sentences);
}

void DBusPebble::resetTimeline()
{
    m_pebble->resetTimeline();
}

qint32 DBusPebble::timelineWindowStart() const
{
    return m_pebble->timelineWindowStart();
}
qint32 DBusPebble::timelineWindowFade() const
{
    return m_pebble->timelineWindowFade();
}
qint32 DBusPebble::timelineWindowEnd() const
{
    return m_pebble->timelineWindowEnd();
}
void DBusPebble::setTimelineWindow(qint32 start, qint32 fade, qint32 end)
{
    m_pebble->setTimelineWindow(start,fade,end);
}

/**
 * @brief DBusPebble::insertTimelinePin
 * @param jsonPin
 * Example usage:
 * dbus-send --session --dest=org.rockwork --type=method_call --print-reply
 *  /org/rockwork/XX_XX_XX_XX_XX_XX org.rockwork.Pebble.insertTimelinePin
 *  string:"$(cat pin.json)"
 * where pin.json is file with raw pin json object
 */
void DBusPebble::insertTimelinePin(const QString &jsonPin)
{
    QJsonParseError jpe;
    QJsonDocument json = QJsonDocument::fromJson(jsonPin.toUtf8(),&jpe);
    if(jpe.error != QJsonParseError::NoError) {
        qWarning() << "Cannot parse JSON Pin:" << jpe.errorString() << jsonPin;
        return;
    }
    if(json.isEmpty() || !json.isObject()) {
        qWarning() << "Empty or flat JSON Pin constructed, ignoring" << jsonPin;
        return;
    }
    m_pebble->insertPin(json.object());
}

bool DBusPebble::syncAppsFromCloud() const
{
    return m_pebble->syncAppsFromCloud();
}
void DBusPebble::setSyncAppsFromCloud(bool enable)
{
    m_pebble->setSyncAppsFromCloud(enable);
}

void DBusPebble::setOAuthToken(const QString &token)
{
    m_pebble->setOAuthToken(token);
}
QString DBusPebble::oauthToken() const
{
    return m_pebble->oauthToken();
}
QString DBusPebble::accountName() const
{
    return m_pebble->accountName();
}
QString DBusPebble::accountEmail() const
{
    return m_pebble->accountEmail();
}

void DBusPebble::setWeatherApiKey(const QString &key)
{
    m_pebble->setWeatherApiKey(key);
}
void DBusPebble::setWeatherAltKey(const QString &key)
{
    m_pebble->setWeatherAltKey(key);
}
QString DBusPebble::WeatherAltKey() const
{
    return m_pebble->getWeatherAltKey();
}

void DBusPebble::setWeatherLanguage(const QString &lang)
{
    m_pebble->setWeatherLanguage(lang);
}
QString DBusPebble::WeatherLanguage() const
{
    return m_pebble->getWeatherLanguage();
}

void DBusPebble::setWeatherUnits(const QString &u)
{
    m_pebble->setWeatherUnits(u);
}
QString DBusPebble::WeatherUnits() const
{
    return m_pebble->getWeatherUnits();
}

/**
 * @brief DBusPebble::SetWeatherLocations - Sets the locations for WeatherProvider as well as storing locations in BlobDB.
 * First location MUST be Current Location, although the name could be localized string (Eg. "Aktueller Standort")
 * @param locs a{as} - Array of String Arrays - each nested array represents ["Location Name", "Lattitude", "Longitude"]
 * @example gdbus call -e -d org.rockwork -o /org/rockwork/B0_B4_48_00_00_00 -m org.rockwork.Pebble.SetWeatherLocations "[<['Current Location','0','0']>]"
 * gdbus call -e -d org.rockwork -o /org/rockwork/B0_B4_48_00_00_00 -m org.rockwork.Pebble.SetWeatherLocations "[<['Current Location','0','0']>,<['London','51.508530','-0.125740']>]"
 */
void DBusPebble::SetWeatherLocations(const QVariantList &locs)
{
    m_pebble->setWeatherLocations(locs);
}
QVariantList DBusPebble::WeatherLocations() const
{
    return m_pebble->getWeatherLocations();
}

/**
 * @brief DBusPebble::InjectWeatherData - Injects the data into BlobDB to show in Pebble's Weather WatchApp
 * @param loc_name string - Exact name of the pre-set location
 * @param conditions a{sv} - Current weather conditions for given location
 * @example gdbus call -e -d org.rockwork -o /org/rockwork/B0_B4_48_00_00_00 -m org.rockwork.Pebble.InjectWeatherData "Current Location" \
 *          "{'text':<'Sonnenschein'>,'today_hi':<29>,'today_low':<16>,'temperature':<27>,'today_icon':<7>,'tomorrow_icon':<6>,'tomorrow_hi':<25>,'tomorrow_low':<12>}"
 */
void DBusPebble::InjectWeatherData(const QString &loc_name, const QVariantMap &obj)
{
    if(!obj.contains("text") || !obj.contains("temperature"))
        return;
    m_pebble->injectWeatherConditions(loc_name,obj);
}

bool DBusPebble::DevConnectionEnabled() const
{
    return m_pebble->devConnection()->enabled();
}

quint16 DBusPebble::DevConnListenPort() const
{
    return m_pebble->devConnection()->listenPort();
}

bool DBusPebble::DevConnCloudEnabled() const
{
    return m_pebble->devConnection()->cloudEnabled();
}

void DBusPebble::SetDevConnEnabled(bool enabled)
{
    m_pebble->devConnection()->setEnabled(enabled);
}

void DBusPebble::SetDevConnListenPort(quint16 port)
{
    m_pebble->setDevConListenPort(port);
}

void DBusPebble::SetDevConnCloudEnabled(bool enabled)
{
    m_pebble->setDevConCloudEnabled(enabled);
}

bool DBusPebble::DevConnectionState() const
{
    return m_pebble->devConnection()->serverState();
}

bool DBusPebble::DevConnCloudState() const
{
    return m_pebble->devConnection()->cloudState();
}

QString DBusPebble::startLogDump() const
{
    m_pebble->setDevConLogDump(true);
    return DevConnection::startLogDump();
}
QString DBusPebble::stopLogDump() const
{
    m_pebble->setDevConLogDump(false);
    return DevConnection::stopLogDump();
}
QString DBusPebble::getLogDump() const
{
    return DevConnection::getLogDump();
}
bool DBusPebble::isLogDumping() const
{
    return DevConnection::isLogDumping();
}
void DBusPebble::setLogLevel(int level) const
{
    m_pebble->setDevConLogLevel(level);
}
int DBusPebble::getLogLevel() const
{
    return DevConnection::getLogLevel();
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

void DBusPebble::SendAppData(const QString &uuid, const QVariantMap &data)
{
    m_pebble->sendAppData(QUuid(uuid), QVariantMap(data));
}

void DBusPebble::CloseApp(const QString &uuid)
{
    m_pebble->closeApp(QUuid(uuid));
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

void DBusPebble::LoadLanguagePack(const QString &pblFile) const
{
    m_pebble->loadLanguagePack(pblFile);
}

bool DBusPebble::UpgradingFirmware() const
{
    return m_pebble->upgradingFirmware();
}

QString DBusPebble::SerialNumber() const
{
    return m_pebble->serialNumber();
}

QString DBusPebble::PlatformString() const
{
    return m_pebble->platformString();
}

QString DBusPebble::HardwarePlatform() const
{
    return m_pebble->platformName();
}

QString DBusPebble::SoftwareVersion() const
{
    return m_pebble->softwareVersion();
}

QString DBusPebble::LanguageVersion() const
{
    return QString("%1:%2").arg(m_pebble->language()).arg(QString::number(m_pebble->langVer()));
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

void DBusPebble::onProfileConnectionSwitchChanged(bool connected) {
    if (connected)
        emit ProfileWhenConnectedChanged();
    else
        emit ProfileWhenDisconnectedChanged();
}

QString DBusPebble::ProfileWhenConnected() {
    return m_pebble->profileWhen(true);
}

QString DBusPebble::ProfileWhenDisconnected() {
    return m_pebble->profileWhen(false);
}

void DBusPebble::SetProfileWhenConnected(const QString &profile) {
    qDebug() << "setting connected profile: " << profile;
    m_pebble->setProfileWhen(true, profile);
}

void DBusPebble::SetProfileWhenDisconnected(const QString &profile) {
    qDebug() << "setting disconnected profile: " << profile;
    m_pebble->setProfileWhen(false, profile);
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
