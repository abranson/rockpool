#include "pebble.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"
#include "musicendpoint.h"
#include "phonecallendpoint.h"
#include "appmanager.h"
#include "appmsgmanager.h"
#include "jskit/jskitmanager.h"
#include "appglances.h"
#include "blobdb.h"
#include "appdownloader.h"
#include "screenshotendpoint.h"
#include "firmwaredownloader.h"
#include "watchlogendpoint.h"
#include "core.h"
#include "platforminterface.h"
#include "ziphelper.h"
#include "dataloggingendpoint.h"
#include "devconnection.h"
#include "timelinemanager.h"
#include "timelinesync.h"
#include "voiceendpoint.h"
#include "sendtextapp.h"
#include "weatherapp.h"
#include "weatherprovidertwc.h"
#include "weatherproviderwu.h"
#include "uploadmanager.h"

#include "QDir"
#include <QDateTime>
#include <QStandardPaths>
#include <QSettings>
#include <QTimeZone>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTemporaryFile>

Pebble::Pebble(const QBluetoothAddress &address, QObject *parent):
    QObject(parent),
    m_address(address),
    m_nam(new QNetworkAccessManager(this))
{
    QString watchPath = m_address.toString().replace(':', '_');
    m_storagePath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/" + watchPath + "/";
    m_imagePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/screenshots/Pebble/";

    m_connection = new WatchConnection(this);
    QObject::connect(m_connection, &WatchConnection::watchConnected, this, &Pebble::onPebbleConnected);
    QObject::connect(m_connection, &WatchConnection::watchDisconnected, this, &Pebble::onPebbleDisconnected);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::timeChanged, this, &Pebble::syncTime);

    m_connection->registerEndpointHandler(WatchConnection::EndpointVersion, this, "pebbleVersionReceived");
    m_connection->registerEndpointHandler(WatchConnection::EndpointPhoneVersion, this, "phoneVersionAsked");
    m_connection->registerEndpointHandler(WatchConnection::EndpointFactorySettings, this, "factorySettingsReceived");

    m_dataLogEndpoint = new DataLoggingEndpoint(this, m_connection);

    m_blobDB = new BlobDB(this, m_connection);

    QHash<QString,QStringList> cans = getCannedMessages();
    Core::instance()->platform()->setCannedResponses(cans);
    m_timelineManager = new TimelineManager(this, m_connection);
    QObject::connect(m_timelineManager, &TimelineManager::muteSource, this, &Pebble::muteNotificationSource);
    QObject::connect(m_timelineManager, &TimelineManager::actionTriggered, Core::instance()->platform(), &PlatformInterface::actionTriggered);
    QObject::connect(m_timelineManager, &TimelineManager::removeNotification, Core::instance()->platform(), &PlatformInterface::removeNotification);

    QObject::connect(Core::instance()->platform(), &PlatformInterface::newTimelinePin, this, &Pebble::insertPin);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::delTimelinePin, this, &Pebble::removePin);

    m_musicEndpoint = new MusicEndpoint(this, m_connection);
    m_musicEndpoint->setMusicMetadata(Core::instance()->platform()->musicMetaData());
    QObject::connect(m_musicEndpoint, &MusicEndpoint::musicControlPressed, Core::instance()->platform(), &PlatformInterface::sendMusicControlCommand);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::musicMetadataChanged, m_musicEndpoint, &MusicEndpoint::setMusicMetadata);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::musicPlayStateChanged, m_musicEndpoint, &MusicEndpoint::writePlayState);

    m_phoneCallEndpoint = new PhoneCallEndpoint(this, m_connection);
    QObject::connect(m_phoneCallEndpoint, &PhoneCallEndpoint::hangupCall, Core::instance()->platform(), &PlatformInterface::hangupCall);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::incomingCall, m_phoneCallEndpoint, &PhoneCallEndpoint::incomingCall);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::callStarted, m_phoneCallEndpoint, &PhoneCallEndpoint::callStarted);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::callEnded, m_phoneCallEndpoint, &PhoneCallEndpoint::callEnded);

    m_appGlances = new AppGlances(this, m_connection);
    m_appManager = new AppManager(this, m_connection);
    QObject::connect(m_appManager, &AppManager::appsChanged, this, &Pebble::installedAppsChanged);
    QObject::connect(m_appManager, &AppManager::idMismatchDetected, this, &Pebble::resetPebble);
    QObject::connect(m_appManager, &AppManager::appInserted, this, &Pebble::appInstalled);

    m_timelineSync = new TimelineSync(this,m_timelineManager);
    QObject::connect(m_timelineSync, &TimelineSync::oauthTokenChanged, this, &Pebble::oauthTokenChanged);

    m_appMsgManager = new AppMsgManager(this, m_connection);
    m_jskitManager = new JSKitManager(this, m_connection, m_appManager, m_appMsgManager, this);
    QObject::connect(m_jskitManager, &JSKitManager::openURL, this, &Pebble::openURL);
    QObject::connect(m_jskitManager, &JSKitManager::appNotification, this, &Pebble::insertPin);
    QObject::connect(m_appMsgManager, &AppMsgManager::appStarted, this, &Pebble::appStarted);
    QObject::connect(m_appMsgManager, &AppMsgManager::appButtonPressed, this, &Pebble::onAppButtonPressed);

    m_weatherApp = new WeatherApp(this, getWeatherLocations());
    QObject::connect(m_weatherApp, &WeatherApp::locationsChanged, this, &Pebble::saveWeatherLocations);

    m_appDownloader = new AppDownloader(m_storagePath, this);
    QObject::connect(m_appDownloader, &AppDownloader::downloadFinished, this, &Pebble::appDownloadFinished);

    m_screenshotEndpoint = new ScreenshotEndpoint(this, m_connection, this);
    QObject::connect(m_screenshotEndpoint, &ScreenshotEndpoint::screenshotAdded, this, &Pebble::screenshotAdded);
    QObject::connect(m_screenshotEndpoint, &ScreenshotEndpoint::screenshotRemoved, this, &Pebble::screenshotRemoved);

    m_firmwareDownloader = new FirmwareDownloader(this, m_connection);
    QObject::connect(m_firmwareDownloader, &FirmwareDownloader::updateAvailableChanged, this, &Pebble::slotUpdateAvailableChanged);
    QObject::connect(m_firmwareDownloader, &FirmwareDownloader::upgradingChanged, this, &Pebble::upgradingFirmwareChanged);
    QObject::connect(m_firmwareDownloader, &FirmwareDownloader::layoutsChanged, m_timelineManager, &TimelineManager::reloadLayouts);

    m_logEndpoint = new WatchLogEndpoint(this, m_connection);
    QObject::connect(m_logEndpoint, &WatchLogEndpoint::logsFetched, this, &Pebble::logsDumped);

    m_sendTextApp = new SendTextApp(this, m_connection);
    QObject::connect(m_sendTextApp, &SendTextApp::contactBlobSet, this, &Pebble::saveTextContacts);
    QObject::connect(m_sendTextApp, &SendTextApp::messageBlobSet, this, &Pebble::saveTextMessages);
    QObject::connect(m_sendTextApp, &SendTextApp::sendTextMessage, Core::instance()->platform(), &PlatformInterface::sendTextMessage);
    QObject::connect(m_timelineManager, &TimelineManager::actionSendText, m_sendTextApp, &SendTextApp::handleTextAction);
    setCannedContacts(getCannedContacts(),false);

    m_voiceEndpoint = new VoiceEndpoint(this, m_connection);
    QObject::connect(m_voiceEndpoint, &VoiceEndpoint::sessionSetupRequest, this, &Pebble::voiceSessionRequest);
    QObject::connect(m_voiceEndpoint, &VoiceEndpoint::audioFrame, this, &Pebble::voiceAudioStream);
    QObject::connect(m_voiceEndpoint, &VoiceEndpoint::sessionCloseNotice, this, &Pebble::voiceSessionClose);

    QSettings watchInfo(m_storagePath + "/watchinfo.conf", QSettings::IniFormat);
    m_model = (Model)watchInfo.value("watchModel", (int)ModelUnknown).toInt();

    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("activityParams");
    m_healthParams.setEnabled(settings.value("enabled").toBool());
    m_healthParams.setAge(settings.value("age").toUInt());
    m_healthParams.setHeight(settings.value("height").toInt());
    m_healthParams.setGender((HealthParams::Gender)settings.value("gender").toInt());
    m_healthParams.setWeight(settings.value("weight").toInt());
    m_healthParams.setMoreActive(settings.value("moreActive").toBool());
    m_healthParams.setSleepMore(settings.value("sleepMore").toBool());
    settings.endGroup();

    settings.beginGroup("unitsDistance");
    m_imperialUnits = settings.value("imperialUnits", false).toBool();
    settings.endGroup();

    settings.beginGroup("calendar");
    m_calendarSyncEnabled = settings.value("calendarSyncEnabled", true).toBool();
    settings.endGroup();

    m_weatherProv = nullptr;
    settings.beginGroup(WeatherApp::appConfigKey);
    initWeatherProvider(settings);
    settings.endGroup();

    settings.beginGroup("profileWhen");
    m_profileWhenConnected = settings.value("connected", "").toString();
    m_profileWhenDisconnected = settings.value("disconnected", "").toString();
    settings.endGroup();

    QObject::connect(m_connection, &WatchConnection::watchConnected, this, &Pebble::profileSwitchRequired);
    QObject::connect(m_connection, &WatchConnection::watchDisconnected, this, &Pebble::profileSwitchRequired);
    QObject::connect(this, &Pebble::profileConnectionSwitchChanged, this, &Pebble::profileSwitchRequired);

    m_devConnection = new DevConnection(this, m_connection);
    QObject::connect(m_devConnection, &DevConnection::serverStateChanged, this, &Pebble::devConServerStateChanged);
    QObject::connect(m_devConnection, &DevConnection::cloudStateChanged, this, &Pebble::devConCloudStateChanged);
    settings.beginGroup("devConnection");
    // DeveloperConnection is a backdoor to the pebble, it has no authentication whatsoever.
    // Dont ever enable it automatically, only on-demand by explicit user request!!111
    //m_devConnection->setEnabled(settings.value("enabled", true).toBool());
    m_devConnection->setPort(settings.value("listenPort", 9000).toInt());
    m_devConnection->setCloudEnabled(settings.value("useCloud", true).toBool()); // not implemented yet
    // Custom Logging params
    m_devConnection->setLogLevel(settings.value("logVerbosity", 1).toInt()); // by default suppress debug logging
    if(settings.value("logDump", false).toBool()) // by default dump is not enabled
        qWarning() << "Dumping logs to" << m_devConnection->startLogDump(); // dump logs to file if enabled
    settings.endGroup();

    settings.beginGroup("timeline");
    m_timelineManager->setTimelineWindow(
        // Past boundary of the timeline window - used for pin validation & retention
        settings.value("pastDays",m_timelineManager->daysPast()).toInt(),
        // Time after which undelivered notifications are considered obsolete
        settings.value("eventFadeout",m_timelineManager->secsEventFadeout()).toInt(),
        // Future boundary of the timeline window - used for pin validation & retention
        settings.value("futureDays",m_timelineManager->daysFuture()).toInt()
    );
    settings.endGroup();
}

QBluetoothAddress Pebble::address() const
{
    return m_address;
}

QString Pebble::name() const
{
    return m_name;
}

void Pebble::setName(const QString &name)
{
    m_name = name;
}

QBluetoothLocalDevice::Pairing Pebble::pairingStatus() const
{
    QBluetoothLocalDevice dev;
    return dev.pairingStatus(m_address);
}

bool Pebble::connected() const
{
    return m_connection->isConnected() && !m_serialNumber.isEmpty();
}

void Pebble::connect()
{
    qDebug() << "Connecting to Pebble:" << m_name << m_address.toString();
    m_connection->connectPebble(m_address);
}

QNetworkAccessManager * Pebble::nam() const
{
    return m_nam;
}

BlobDB * Pebble::blobdb() const
{
    return m_blobDB;
}

TimelineSync * Pebble::tlSync() const
{
    return m_timelineSync;
}

TimelineManager * Pebble::timeline() const
{
    return m_timelineManager;
}

AppGlances * Pebble::appGlances() const
{
    return m_appGlances;
}

bool Pebble::syncAppsFromCloud() const
{
    return m_timelineSync->syncFromCloud();
}
void Pebble::setSyncAppsFromCloud(bool enable)
{
    m_timelineSync->setSyncFromCloud(enable);
}

void Pebble::setOAuthToken(const QString &token)
{
    m_timelineSync->setOAuthToken(token);
}

const QString Pebble::oauthToken() const
{
    return m_timelineSync->oauthToken();
}
const QString Pebble::accountName() const
{
    return m_timelineSync->accountName();
}
const QString Pebble::accountEmail() const
{
    return m_timelineSync->accountEmail();
}

QDateTime Pebble::softwareBuildTime() const
{
    return m_softwareBuildTime;
}

QString Pebble::softwareVersion() const
{
    return m_softwareVersion;
}

QString Pebble::softwareCommitRevision() const
{
    return m_softwareCommitRevision;
}

HardwareRevision Pebble::hardwareRevision() const
{
    return m_hardwareRevision;
}

Model Pebble::model() const
{
    return m_model;
}

void Pebble::setHardwareRevision(HardwareRevision hardwareRevision)
{
    m_hardwareRevision = hardwareRevision;
    switch (m_hardwareRevision) {
    case HardwareRevisionTINTIN_EV1:
    case HardwareRevisionTINTIN_EV2:
    case HardwareRevisionTINTIN_EV2_3:
    case HardwareRevisionTINTIN_EV2_4:
    case HardwareRevisionTINTIN_V1_5:
    case HardwareRevisionBIANCA:
    case HardwareRevisionTINTIN_BB:
    case HardwareRevisionTINTIN_BB2:
        m_hardwarePlatform = HardwarePlatformAplite;
        break;
    case HardwareRevisionSNOWY_EVT2:
    case HardwareRevisionSNOWY_DVT:
    case HardwareRevisionBOBBY_SMILES:
    case HardwareRevisionSNOWY_BB:
    case HardwareRevisionSNOWY_BB2:
        m_hardwarePlatform = HardwarePlatformBasalt;
        break;
    case HardwareRevisionSPALDING_EVT:
    case HardwareRevisionSPALDING:
    case HardwareRevisionSPALDING_BB2:
        m_hardwarePlatform = HardwarePlatformChalk;
        break;
    case HardwareRevisionSILK:
    case HardwareRevisionSILK_BB:
    case HardwareRevisionSILK_BB2:
    case HardwareRevisionSILK_EVT:
        m_hardwarePlatform = HardwarePlatformDiorite;
        break;
    case HardwareRevisionROBERT_EVT:
    case HardwareRevisionROBERT_BB:
        m_hardwarePlatform = HardwarePlatformEmery;
        break;
    default:
        m_hardwarePlatform = HardwarePlatformUnknown;
    }
}

QString Pebble::platformString() const
{
    switch (m_hardwareRevision) {
    case HardwareRevisionUNKNOWN:
    case HardwareRevisionTINTIN_EV1:
    case HardwareRevisionTINTIN_EV2:
    case HardwareRevisionTINTIN_EV2_3:
    case HardwareRevisionSNOWY_EVT2:
    case HardwareRevisionSPALDING_EVT:
    case HardwareRevisionTINTIN_BB:
    case HardwareRevisionTINTIN_BB2:
    case HardwareRevisionSNOWY_BB:
    case HardwareRevisionSNOWY_BB2:
    case HardwareRevisionSPALDING_BB2:
    case HardwareRevisionSILK_BB:
    case HardwareRevisionSILK_BB2:
    case HardwareRevisionROBERT_BB:
        break;
    case HardwareRevisionTINTIN_EV2_4:
        return "ev2_4";
    case HardwareRevisionTINTIN_V1_5:
        return "v1_5";
    case HardwareRevisionBIANCA:
        return "v2_0";
    case HardwareRevisionSNOWY_DVT:
        return "snowy_dvt";
    case HardwareRevisionBOBBY_SMILES:
        return "snowy_s3";
    case HardwareRevisionSPALDING:
        return "spalding";
    case HardwareRevisionSILK_EVT:
        return "silk_evt";
    case HardwareRevisionSILK:
        return "silk";
    case HardwareRevisionROBERT_EVT:
        return "robert_evt";
    }
    return QString();
}

HardwarePlatform Pebble::hardwarePlatform() const
{
    return m_hardwarePlatform;
}

QString Pebble::platformName() const
{
    switch(m_hardwarePlatform) {
        case HardwarePlatformAplite:
            return "aplite";
        case HardwarePlatformBasalt:
            return "basalt";
        case HardwarePlatformChalk:
            return "chalk";
        case HardwarePlatformDiorite:
            return "diorite";
        case HardwarePlatformEmery:
            return "emery";
        default:
            return "unknown";
    }
}

QString Pebble::serialNumber() const
{
    return m_serialNumber;
}

QString Pebble::language() const
{
    return m_language;
}
quint16 Pebble::langVer() const
{
    return m_langVer;
}

Capabilities Pebble::capabilities() const
{
    return m_capabilities;
}

bool Pebble::isUnfaithful() const
{
    return m_isUnfaithful;
}

bool Pebble::recovery() const
{
    return m_recovery;
}

bool Pebble::upgradingFirmware() const
{
    return m_firmwareDownloader->upgrading();
}

DevConnection * Pebble::devConnection()
{
    return m_devConnection;
}
void Pebble::setDevConListenPort(quint16 port)
{
    m_devConnection->setPort(port);
    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("devConnection");
    settings.setValue("listenPort", port);
}
void Pebble::setDevConCloudEnabled(bool enabled)
{
    m_devConnection->setCloudEnabled(enabled);
    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("devConnection");
    settings.setValue("useCloud", enabled);
}
void Pebble::setDevConLogLevel(int level)
{
    DevConnection::setLogLevel(level);
    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("devConnection");
    settings.setValue("logVerbosity", level);
}
void Pebble::setDevConLogDump(bool enable)
{
    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("devConnection");
    settings.setValue("logDump", enable);
}

qint32 Pebble::timelineWindowStart() const
{
    return m_timelineManager->daysPast();
}
qint32 Pebble::timelineWindowFade() const
{
    return m_timelineManager->secsEventFadeout();
}
qint32 Pebble::timelineWindowEnd() const
{
    return m_timelineManager->daysFuture();
}
void Pebble::setTimelineWindow(qint32 start, qint32 fade, qint32 end)
{
    if(start>=0 || start > end) {
        qWarning() << "Ignoring invalid timeline window: start" << start << "end" << end;
        return;
    }
    if(fade > 0)
        fade = -fade;
    m_timelineManager->setTimelineWindow(start,fade,end);
    QSettings setts(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    setts.beginGroup("timeline");
    setts.setValue("daysPast",start);
    setts.setValue("eventFadeout",fade);
    setts.setValue("futureDays",end);
    // Since window has changed try to re-sync timeline
    emit m_timelineSync->syncUrlChanged("");
    syncCalendar(); // and calendar
}

void Pebble::setHealthParams(const HealthParams &healthParams)
{
    m_healthParams = healthParams;
    m_blobDB->insert(BlobDB::BlobDBIdAppSettings,healthParams);
    emit healtParamsChanged();

    QSettings healthSettings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    healthSettings.beginGroup("activityParams");
    healthSettings.setValue("enabled", m_healthParams.enabled());
    healthSettings.setValue("age", m_healthParams.age());
    healthSettings.setValue("height", m_healthParams.height());
    healthSettings.setValue("gender", m_healthParams.gender());
    healthSettings.setValue("weight", m_healthParams.weight());
    healthSettings.setValue("moreActive", m_healthParams.moreActive());
    healthSettings.setValue("sleepMore", m_healthParams.sleepMore());

}

HealthParams Pebble::healthParams() const
{
    return m_healthParams;
}

void Pebble::setImperialUnits(bool imperial)
{
    m_imperialUnits = imperial;
    m_blobDB->setUnits(imperial);
    emit imperialUnitsChanged();

    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("unitsDistance");
    settings.setValue("enabled", m_imperialUnits);
}

bool Pebble::imperialUnits() const
{
    return m_imperialUnits;
}

void Pebble::setWeatherUnits(const QString &u)
{
    if(m_weatherProv)
        m_weatherProv->setUnits(u.at(0));
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    appCfg.beginGroup(WeatherApp::appConfigKey);
    appCfg.setValue("units",u);
    appCfg.endGroup();
}
QString Pebble::getWeatherUnits() const
{
    return m_weatherProv ? m_weatherProv->getUnits() : 'm';
}

void Pebble::setWeatherApiKey(const QString &key)
{
    //((WebWeatherProvider*)m_weatherProv)->setApiKey(key);
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    appCfg.beginGroup(WeatherApp::appConfigKey);
    appCfg.setValue("apiKey",key);
    initWeatherProvider(appCfg);
    appCfg.endGroup();
}

void Pebble::setWeatherAltKey(const QString &key)
{
    //((WebWeatherProvider*)m_weatherProv)->setApiKey(key);
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    appCfg.beginGroup(WeatherApp::appConfigKey);
#ifdef WEATHERPROVIDERWU_H
    appCfg.setValue("wuKey",key);
#endif
    initWeatherProvider(appCfg);
    appCfg.endGroup();
}
QString Pebble::getWeatherAltKey() const
{
    QString key;
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    appCfg.beginGroup(WeatherApp::appConfigKey);
#ifdef WEATHERPROVIDERWU_H
    key = appCfg.value("wuKey").toString();
#endif
    appCfg.endGroup();
    return key;
}

void Pebble::initWeatherProvider(const QSettings &settings)
{
    QString apiKey;
#define INIT_WEATHER_PROV(key,type) \
    if(settings.contains(key) && !settings.value(key).toString().isEmpty()) {\
        if(m_weatherProv == nullptr || dynamic_cast<type*>(m_weatherProv) == nullptr) {\
            if(m_weatherProv) delete m_weatherProv;\
            m_weatherProv = new type(this,m_connection,m_weatherApp);\
        }\
        apiKey = settings.value(key).toString();\
    }
#ifdef  WEATHERPROVIDERWU_H
    /*
    if(settings.contains("wuKey") && !settings.value("wuKey").toString().isEmpty()) {
        if(m_weatherProv == nullptr || dynamic_cast<WeatherProviderWU*>(m_weatherProv) == nullptr) {
            if(m_weatherProv) delete m_weatherProv;
            m_weatherProv = new WeatherProviderWU(this,m_connection,m_weatherApp);
        }
        apiKey = settings.value("wuKey").toString();
    }*/
    INIT_WEATHER_PROV("wuKey",WeatherProviderWU)
    else
#endif//WEATHERPROVIDERWU_H
    /*
    if(!settings.value("apiKey").toString().isEmpty()) {
        if(m_weatherProv) delete m_weatherProv;
        m_weatherProv = new WeatherProviderTWC(this,m_connection,m_weatherApp);
        apiKey = settings.value("apiKey").toString();
    }*/
    INIT_WEATHER_PROV("apiKey",WeatherProviderTWC)
    if(m_weatherProv != nullptr) {
        m_weatherProv->setUnits(settings.value("units",'m').toChar());
        m_weatherProv->setLanguage(settings.value("language").toString());
    }
    if(!apiKey.isEmpty())
        ((WebWeatherProvider*)m_weatherProv)->setApiKey(apiKey);
}

void Pebble::setWeatherLanguage(const QString &lang)
{
    if(m_weatherProv)
        m_weatherProv->setLanguage(lang);
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    appCfg.beginGroup(WeatherApp::appConfigKey);
    appCfg.setValue("language",lang);
    appCfg.endGroup();
}
QString Pebble::getWeatherLanguage() const
{
    return m_weatherProv ? m_weatherProv->getLanguage() : "";
}

QVariantList Pebble::getWeatherLocations() const
{
    QVariantList locs;
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    int n = appCfg.beginReadArray(WeatherApp::appConfigKey);
    for(int i=0;i<n;i++) {
        QStringList city;
        appCfg.setArrayIndex(i);
        city.append(appCfg.value("name").toString());
        city.append(appCfg.value("lat").toString());
        city.append(appCfg.value("lng").toString());
        locs.append(city);
    }
    appCfg.endArray();
    qDebug() << "Deserialized" << locs.size() << "locations";
    return locs;
}

void Pebble::setWeatherLocations(const QVariantList &locs)
{
    m_weatherApp->setLocations(locs);
}

void Pebble::saveWeatherLocations() const
{
    QVariantList locs = m_weatherApp->getLocations();
    QSettings appCfg(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    appCfg.beginWriteArray(WeatherApp::appConfigKey,locs.count());
    for(int i=0;i<locs.count();i++) {
        QStringList city = locs.at(i).toStringList();
        appCfg.setArrayIndex(i);
        appCfg.setValue("name",city.first());
        appCfg.setValue("lat",city.at(1));
        appCfg.setValue("lng",city.at(2));
    }
    appCfg.endArray();
    emit weatherLocationsChanged(locs);
}

void Pebble::injectWeatherConditions(const QString &location, const QVariantMap &twc)
{
    WeatherApp::Observation obs;
    obs.text = twc.value("text").toString();
    obs.temp_now = twc.value("temperature",SHRT_MAX).toInt();
    obs.timestamp = twc.value("ts",QDateTime::currentDateTime()).toDateTime();
    obs.today.temp_hi = twc.value("today_hi",SHRT_MAX).toInt();
    obs.today.temp_low = twc.value("today_low",SHRT_MAX).toInt();
    obs.today.condition = twc.value("today_icon").toInt();
    obs.tomorrow.temp_hi = twc.value("tomorrow_hi",SHRT_MAX).toInt();
    obs.tomorrow.temp_low = twc.value("tomorrow_low",SHRT_MAX).toInt();
    obs.tomorrow.condition = twc.value("tomorrow_icon").toInt();
    m_weatherApp->injectObservation(location, obs, true);
}

void Pebble::dumpLogs(const QString &fileName) const
{
    m_logEndpoint->fetchLogs(fileName);
}

QString Pebble::storagePath() const
{
    return m_storagePath;
}

QString Pebble::imagePath() const
{
    return m_imagePath;
}

void Pebble::voiceSessionRequest(const QUuid &appUuid, const SpeexInfo &codec)
{
    if(m_voiceSessDump) {
        m_voiceEndpoint->sessionSetupResponse(VoiceEndpoint::ResInvalidMessage,appUuid);
        return;
    }
    m_voiceSessDump = new QTemporaryFile(this);
    if(m_voiceSessDump->open()) {
        QString mime = QString("audio/speex; rate=%1; bitrate=%2; bitstreamver=%3; frame=%4").arg(
                QString::number(codec.sampleRate),
                QString::number(codec.bitRate),
                QString::number(codec.bitstreamVer),
                QString::number(codec.frameSize));
        emit voiceSessionSetup(m_voiceSessDump->fileName(),mime,appUuid.toString());
        m_voiceEndpoint->sessionSetupResponse(VoiceEndpoint::ResSuccess,appUuid);
        qDebug() << "Opened session for" << mime << "to" << m_voiceSessDump->fileName() << "from" << appUuid.toString();
    } else {
        m_voiceSessDump->deleteLater();
        m_voiceEndpoint->sessionSetupResponse(VoiceEndpoint::ResServiceUnavail,appUuid);
        m_voiceSessDump = nullptr;
    }
}
void Pebble::voiceSessionClose(quint16 sesId)
{
    qDebug() << "Cleaning up and forwarding further closure of session" << sesId << m_voiceSessDump->fileName();
    emit voiceSessionClosed(m_voiceSessDump->fileName());
    m_voiceSessDump->deleteLater();
    m_voiceSessDump = 0;
}
void Pebble::voiceAudioStream(quint16 sid, const AudioStream &frames)
{
    Q_UNUSED(sid);
    if(frames.count>0) {
        if(m_voiceSessDump->pos()==0) {
            emit voiceSessionStream(m_voiceSessDump->fileName());
            qDebug() << "Audio Stream has started dumping to" << m_voiceSessDump->fileName();
        }
        for(int i=0;i<frames.count;i++) {
            m_voiceSessDump->write(frames.frames.at(i).data);
        }
    } else {
        qDebug() << "Audio Stream has finished dumping to" << m_voiceSessDump->fileName();
        m_voiceSessDump->close();
        emit voiceSessionDumped(m_voiceSessDump->fileName());
    }
}
void Pebble::voiceAudioStop()
{
    if(m_voiceSessDump->pos()==0) {
        qDebug() << "Audio transfer didn't happen at current session, cleaning up" << m_voiceSessDump->fileName();
        m_voiceEndpoint->sessionSetupResponse(VoiceEndpoint::ResRecognizerError,m_voiceSessDump->property("uuid").toUuid());
        m_voiceSessDump->deleteLater();
        emit voiceSessionClosed(m_voiceSessDump->fileName());
        m_voiceSessDump = nullptr;
    } else {
        m_voiceEndpoint->stopAudioStream();
    }
}
void Pebble::voiceSessionResult(const QString &fileName, const QVariantList &sentences)
{
    if(m_voiceSessDump && m_voiceSessDump->fileName() == fileName) {
        QList<VoiceEndpoint::Sentence> data;
        foreach (const QVariant &vl, sentences) {
            VoiceEndpoint::Sentence st;
            QVariantList vs = vl.toList();
            st.count=vs.count();
            foreach (const QVariant &v, vs) {
                VoiceEndpoint::Word word;
                if(v.canConvert(QMetaType::QVariantMap)) {
                    word.confidence = v.toMap().value("confidence").toInt();
                    word.data = v.toMap().value("word").toString().toUtf8();
                } else if(v.canConvert(QMetaType::QVariantList)) {
                    word.confidence = v.toList().first().toUInt();
                    word.data = v.toList().last().toString().toUtf8();
                }
                word.length = word.data.length();
                st.words.append(word);
            }
            data.append(st);
        }
        m_voiceEndpoint->transcriptionResponse(VoiceEndpoint::ResSuccess, data, QUuid());
    }
}

QVariantMap Pebble::cannedMessages() const
{
    QVariantMap ret;
    QHash<QString,QStringList> cans = Core::instance()->platform()->cannedResponses();
    foreach(const QString &grp,cans.keys())
        ret.insert(grp,QVariant(cans.value(grp)));
    return ret;
}

void inline saveMsgs(QSettings &cMsgs, const QStringList &msgs, const QString &grp)
{
    cMsgs.beginWriteArray(grp,msgs.count());
    for(int i=0;i<msgs.count();i++) {
        cMsgs.setArrayIndex(i);
        cMsgs.setValue("msg",msgs.at(i));
    }
    cMsgs.endArray();
}

void Pebble::setCannedMessages(const QVariantMap &cans) const
{
    QSettings cMsgs(m_storagePath + "/canned_messages.conf", QSettings::IniFormat);
    QHash<QString,QStringList> pass;
    foreach(const QString &grp,cans.keys()) {
        QStringList msgs = cans.value(grp).toStringList();
        pass.insert(grp,msgs);
        // Skip cans stored on pebble - need ACK to save
        if(SendTextApp::appKeys.contains(grp.toUtf8())) continue;
        saveMsgs(cMsgs,msgs,grp);
    }
    Core::instance()->platform()->setCannedResponses(pass);
    m_sendTextApp->setCannedMessages(pass);
}

void Pebble::saveTextMessages(const QByteArray &key) const
{
    QSettings cMsgs(m_storagePath + "/canned_messages.conf", QSettings::IniFormat);
    QStringList msgs = m_sendTextApp->getCannedMessages(key);
    saveMsgs(cMsgs,msgs,QString::fromUtf8(key));
    emit messagesChanged();
}

QHash<QString,QStringList> Pebble::getCannedMessages(const QStringList &groups) const
{
    QHash<QString,QStringList> cans;
    QSettings cMsgs(m_storagePath + "/canned_messages.conf", QSettings::IniFormat);
    foreach(const QString &grp,cMsgs.childGroups()) {
        if(groups.isEmpty() || groups.contains(grp)) {
            int msgn = cMsgs.beginReadArray(grp);
            QStringList msgs;
            for(int i=0;i<msgn;i++) {
                cMsgs.setArrayIndex(i);
                if(cMsgs.contains("msg"))
                    msgs.append(cMsgs.value("msg").toString());
            }
            cMsgs.endArray();
            if(!msgs.isEmpty())
                cans.insert(grp,msgs);
        }
    }
    return cans;
}

void Pebble::setCannedContacts(const QMap<QString, QStringList> &cans, bool push)
{
    QList<SendTextApp::Contact> ret;
    for(QMap<QString,QStringList>::const_iterator it=cans.begin();it!=cans.end();it++) {
        ret.append(SendTextApp::Contact({it.key(),it.value()}));
    }
    m_sendTextApp->setCannedContacts(ret,push);
}
void Pebble::saveTextContacts() const
{
    QList<SendTextApp::Contact> ctxs = m_sendTextApp->getCannedContacts();
    QStringList all=getCannedContacts().keys();
    QSettings cCtx(m_storagePath + "/canned_messages.conf", QSettings::IniFormat);
    foreach(const SendTextApp::Contact c, ctxs) {
        cCtx.beginWriteArray(c.name,c.numbers.length());
        for(int i=0;i<c.numbers.length();i++) {
            cCtx.setArrayIndex(i);
            cCtx.setValue("ctx",c.numbers.at(i));
        }
        cCtx.endArray();
        all.removeAll(c.name);
    }
    foreach (const QString &c, all) {
        cCtx.beginGroup(c);
        cCtx.remove("");
        cCtx.endGroup();
    }
    emit contactsChanged();
}

QMap<QString,QStringList> Pebble::getCannedContacts(const QStringList &people) const
{
    QMap<QString,QStringList> ret;
    QSettings cCtx(m_storagePath + "/canned_messages.conf", QSettings::IniFormat);
    foreach (const QString &name, cCtx.childGroups()) {
        if(people.isEmpty() || people.contains(name)) {
            int ctxn = cCtx.beginReadArray(name);
            for(int i=0;i<ctxn;i++) {
                cCtx.setArrayIndex(i);
                if(cCtx.contains("ctx"))
                    ret[name].append(cCtx.value("ctx").toString());
            }
            cCtx.endArray();
        }
    }
    return ret;
}

QVariantMap Pebble::notificationsFilter() const
{
    QVariantMap ret;
    QString settingsFile = m_storagePath + "/notifications.conf";
    QSettings s(settingsFile, QSettings::IniFormat);

    foreach (const QString &group, s.childGroups()) {
        s.beginGroup(group);
        QVariantMap notif;
        notif.insert("enabled", Pebble::NotificationFilter(s.value("enabled").toInt()));
        notif.insert("icon", s.value("icon").toString());
        notif.insert("name", s.value("name").toString());
        ret.insert(group, notif);
        s.endGroup();
    }
    return ret;
}

void Pebble::setNotificationFilter(const QString &sourceId, const NotificationFilter enabled)
{
    QString settingsFile = m_storagePath + "/notifications.conf";
    QSettings s(settingsFile, QSettings::IniFormat);
    s.beginGroup(sourceId);
    if (s.value("enabled").toInt() != enabled) {
        s.setValue("enabled", enabled);
        emit notificationFilterChanged(sourceId, s.value("name").toString(), s.value("icon").toString(), enabled);
    }
    s.endGroup();
}

void Pebble::forgetNotificationFilter(const QString &sourceId) {
    if (sourceId.isEmpty()) return; // don't remove everything by accident
    QString settingsFile = m_storagePath + "/notifications.conf";
    QSettings s(settingsFile, QSettings::IniFormat);
    s.remove(sourceId);
    emit notificationFilterChanged(sourceId, "", "", NotificationForgotten);
}

void Pebble::setNotificationFilter(const QString &sourceId, const QString &name, const QString &icon, const NotificationFilter enabled)
{
    QString settingsFile = m_storagePath + "/notifications.conf";
    QSettings s(settingsFile, QSettings::IniFormat);
    qDebug() << "Setting" << sourceId << ":" << name << "with icon" << icon << "to" << enabled;
    bool changed = false;
    s.beginGroup(sourceId);
    if (s.value("enabled").toInt() != enabled) {
        s.setValue("enabled", enabled);
        changed = true;
    }

    if (!icon.isEmpty() && s.value("icon").toString() != icon) {
        s.setValue("icon", icon);
        changed = true;
    }
    else if (s.value("icon").toString().isEmpty()) {
        s.setValue("icon", findNotificationData(sourceId, "Icon"));
        changed = true;
    }

    if (!name.isEmpty() && s.value("name").toString() != name) {
        s.setValue("name", name);
        changed = true;
    }
    else if (s.value("name").toString().isEmpty()) {
        s.setValue("name", findNotificationData(sourceId, "Name"));
        changed = true;
    }

    if (changed)
        emit notificationFilterChanged(sourceId, s.value("name").toString(), s.value("icon").toString(), enabled);
    s.endGroup();

}

QString Pebble::findNotificationData(const QString &sourceId, const QString &key)
{
    qDebug() << "Looking for notification" << key << "for" << sourceId << "in launchers.";
    QStringList appsDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    foreach (const QString &appsDir, appsDirs) {
        QDir dir(appsDir);
        QFileInfoList entries = dir.entryInfoList({"*.desktop"});
        foreach (const QFileInfo &appFile, entries) {
            QSettings s(appFile.absoluteFilePath(), QSettings::IniFormat);
            s.beginGroup("Desktop Entry");
            if (appFile.baseName() == sourceId
                    || s.value("Exec").toString() == sourceId
                    || s.value("X-apkd-packageName").toString() == sourceId) {
                return s.value(key).toString();
            }
        }
    }
    return 0;
}

void Pebble::insertPin(const QJsonObject &json)
{
    QJsonObject pinObj(json);
    if(!pinObj.contains("guid")) {
        QUuid guid;
        if(pinObj.contains("id")) {
            guid = PlatformInterface::idToGuid(pinObj.value("id").toString());
        } else {
            guid = QUuid::createUuid();
            qWarning() << "Neither GUID nor ID field present, generating random, pin control will be limited";
        }
        pinObj.insert("guid",guid.toString().mid(1,36));
    }
    if(pinObj.contains("type") && pinObj.value("type").toString() == "notification") {
        QStringList dataSource = pinObj.value("dataSource").toString().split(":");
        if(dataSource.count() > 1) {
            QString parent = dataSource.takeLast();
            QString sourceId = dataSource.first();
            if(dataSource.count() > 1) { // Escape colon in the srcId
                sourceId = dataSource.join("%3A");
                pinObj.insert("dataSource",QString("%1:%2").arg(sourceId).arg(parent));
            }
            QVariantMap notifFilter = notificationsFilter().value(sourceId).toMap();
            NotificationFilter f = NotificationFilter(notifFilter.value("enabled", QVariant(NotificationEnabled)).toInt());
            if (f==NotificationDisabled || (f==Pebble::NotificationDisabledActive && Core::instance()->platform()->deviceIsActive())) {
                qDebug() << "Notifications for" << sourceId << "disabled.";
                return;
            }
            // In case it wasn't there before, make sure to write it to the config now so it will appear in the config app.
            setNotificationFilter(sourceId, pinObj.value("source").toString(), pinObj.value("sourceIcon").toString(), NotificationEnabled);
            // Build mute action so that we can mute event passing through this section
            QJsonArray actions = pinObj.value("actions").toArray();
            QJsonObject mute;
            mute.insert("type",QString("mute"));
            QString sender = pinObj.contains("sourceName") ? pinObj.value("sourceName").toString() : pinObj.value("source").toString();
            mute.insert("title",QString(sender.isEmpty()?"Mute":"Mute "+sender));
            actions.append(mute);
            pinObj.insert("actions",actions);
        }
    }
    if(!pinObj.contains("dataSource")) {
        QString parent = PlatformInterface::idToGuid("dbus").toString().mid(1,36);
        if(pinObj.contains("source")) {
            pinObj.insert("dataSource",QString("%1:%2").arg(pinObj.value("source").toString()).arg(parent));
        } else {
            pinObj.insert("dataSource",QString("genericDbus:%1").arg(parent));
        }
    }
    // These must be present for retention and updates
    if(!pinObj.contains("createTime"))
        pinObj.insert("createTime",QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if(!pinObj.contains("updateTime"))
        pinObj.insert("updateTime",QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    qDebug() << "Inserting pin" << QJsonDocument(pinObj).toJson();
    m_timelineManager->insertTimelinePin(pinObj);
}
void Pebble::removePin(const QString &guid)
{
    m_timelineManager->removeTimelinePin(guid);
}

void Pebble::clearAppDB()
{
    m_appManager->clearApps();
}

void Pebble::clearTimeline()
{
    m_timelineManager->wipeTimeline();
}

void Pebble::syncCalendar()
{
    Core::instance()->platform()->syncOrganizer(m_timelineManager->daysFuture());
}

void Pebble::setCalendarSyncEnabled(bool enabled)
{
    if (m_calendarSyncEnabled == enabled) {
        return;
    }
    m_calendarSyncEnabled = enabled;
    emit calendarSyncEnabledChanged();
    qDebug() << "Changing calendar sync enabled to" << enabled;

    if (!m_calendarSyncEnabled) {
        Core::instance()->platform()->stopOrganizer();
        m_timelineManager->clearTimeline(PlatformInterface::UUID);
    } else {
        syncCalendar();
    }

    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("calendar");
    settings.setValue("calendarSyncEnabled", m_calendarSyncEnabled);
    settings.endGroup();
}

bool Pebble::calendarSyncEnabled() const
{
    return m_calendarSyncEnabled;
}

QString Pebble::profileWhen(bool connected) const {
    if (connected)
        return m_profileWhenConnected;
    else
        return m_profileWhenDisconnected;
}

void Pebble::setProfileWhen(const bool connected, const QString &profile)
{
    QString *profileWhen = connected?&m_profileWhenConnected:&m_profileWhenDisconnected;
    if (profileWhen == profile) {
        return;
    }
    *profileWhen = profile;
    emit profileConnectionSwitchChanged(connected);

    QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
    settings.beginGroup("profileWhen");

    settings.setValue(connected?"connected":"disconnected", *profileWhen);
    settings.endGroup();
}

void Pebble::installApp(const QString &id)
{
    m_appDownloader->downloadApp(id);
}

void Pebble::sideloadApp(const QString &packageFile)
{
    QString targetFile = packageFile;
    targetFile.remove("file://");

    QString id;
    QTemporaryDir td;
    if(td.isValid()) {
        if (!ZipHelper::unpackArchive(targetFile, td.path())) {
            qWarning() << "Error unpacking App zip file" << targetFile << "to" << td.path();
            return;
        }
        qDebug() << "Pre-scanning app" << td.path();
        AppInfo info(td.path());
        if (!info.isValid()) {
            qWarning() << "Error parsing App metadata" << targetFile << "at" << td.path();
            return;
        }
        if(installedAppIds().contains(info.uuid())) {
            id = appInfo(info.uuid()).storeId(); // Existing app, upgrade|downgrade|overwrite
        } else {
            id = info.uuid().toString().mid(1,36); // Install new app under apps/uuid/
            QDir ad;
            if(!ad.mkpath(m_storagePath + "apps/" + id)) {
                qWarning() << "Cannot create app dir" << m_storagePath + "apps/" + id;
                return;
            }
        }

        if(!ZipHelper::unpackArchive(targetFile, m_storagePath + "apps/" + id)) {
                qWarning() << "Error unpacking App zip file" << targetFile << "to" << m_storagePath + "apps/" + id;
                return;
        }
        qDebug() << "Sideload package unpacked to" << m_storagePath + "apps/" + id;
        // Store the file. Sideloaded file will likely be of the same name/version
        QString newFile = m_storagePath + "apps/" + id + "/v"+info.versionLabel()+".pbw";
        if(QFile::exists(newFile))
            QFile::remove(newFile);
        QFile::rename(targetFile,newFile);
        appDownloadFinished(id);
    }
}

QList<QUuid> Pebble::installedAppIds()
{
    return m_appManager->appUuids();
}

void Pebble::setAppOrder(const QList<QUuid> &newList)
{
    m_appManager->setAppOrder(newList);
}

AppInfo Pebble::appInfo(const QUuid &uuid)
{
    return m_appManager->info(uuid);
}

AppInfo Pebble::currentApp()
{
    return m_jskitManager->currentApp();
}

void Pebble::removeApp(const QUuid &uuid)
{
    qDebug() << "Should remove app:" << uuid;
    m_appManager->wipeApp(uuid,true);
    m_timelineSync->syncLocker(true);
}

void Pebble::sendAppData(const QUuid &uuid, const QVariantMap &data)
{
    m_appMsgManager->send(uuid, data);
}

void Pebble::closeApp(const QUuid &uuid)
{
    m_appMsgManager->closeApp(uuid);
}

void Pebble::launchApp(const QUuid &uuid)
{
    m_appMsgManager->launchApp(uuid);
}

void Pebble::requestConfigurationURL(const QUuid &uuid) {
    if (m_jskitManager->currentApp().uuid() == uuid) {
        m_jskitManager->showConfiguration();
    }
    else {
        m_jskitManager->setConfigurationId(uuid);
        m_appMsgManager->launchApp(uuid);
    }
}

void Pebble::configurationClosed(const QUuid &uuid, const QString &result)
{
    if (m_jskitManager->currentApp().uuid() == uuid) {
        m_jskitManager->handleWebviewClosed(result);
    }
}

void Pebble::requestScreenshot()
{
    m_screenshotEndpoint->requestScreenshot();
}

QStringList Pebble::screenshots() const
{
    return m_screenshotEndpoint->screenshots();
}

void Pebble::removeScreenshot(const QString &filename)
{
    m_screenshotEndpoint->removeScreenshot(filename);
}

bool Pebble::firmwareUpdateAvailable() const
{
    return m_firmwareDownloader->updateAvailable();
}

QString Pebble::candidateFirmwareVersion() const
{
    return m_firmwareDownloader->candidateVersion();
}

QString Pebble::firmwareReleaseNotes() const
{
    return m_firmwareDownloader->releaseNotes();
}

void Pebble::upgradeFirmware() const
{
    m_firmwareDownloader->performUpgrade();
}

void Pebble::loadLanguagePack(const QString &pblFile) const
{
    QString targetFile = m_storagePath + "lang";
    if(pblFile != targetFile) {
        if(QFile::exists(targetFile))
            QFile::remove(targetFile);
        if(pblFile.startsWith("https://") || pblFile.startsWith("http://")) {
            QNetworkReply *rpl = m_nam->get(QNetworkRequest(QUrl(pblFile)));
            QObject::connect(rpl, &QNetworkReply::finished,[this,rpl](){
                rpl->deleteLater();
                if(rpl->error() == QNetworkReply::NoError && rpl->header(QNetworkRequest::ContentTypeHeader).toString() == "binary/octet-stream") {
                    QString pblName = m_storagePath + "lang";
                    QFile pblFile(pblName);
                    if(pblFile.open(QIODevice::ReadWrite)) {
                        pblFile.write(rpl->readAll());
                        pblFile.close();
                        loadLanguagePack(pblName);
                    }
                }
            });
            qDebug() << "Fetching pbl from" << pblFile;
            return;
        } else if(pblFile.startsWith("file://")) {
            if(pblFile.mid(7) != targetFile)
                QFile::copy(pblFile.mid(7),targetFile);
        } else if(!targetFile.startsWith("/")) {
            qWarning() << "Unknown schema or relative path in file" << pblFile;
            return;
        } else
            QFile::copy(pblFile,targetFile);
    }
    m_connection->uploadManager()->upload(WatchConnection::UploadTypeFile,0,WatchConnection::UploadTypeFile,targetFile,-1,STM_CRC_INIT,
    [this,pblFile](){
        qDebug() << "Successfully uploaded" << pblFile;
        emit languagePackChanged();
    },
    [pblFile](int code){
        qWarning() << "Error" << code << "uploading" << pblFile;
    });
}

void Pebble::onPebbleConnected()
{
    qDebug() << "Pebble connected:" << m_name;
    QByteArray data;
    WatchDataWriter w(&data);
    w.write<quint8>(0); // Command fetch
    QString message = "mfg_color";
    w.writeLE<quint8>(message.length());
    w.writeFixedString(message.length(), message);
    m_connection->writeToPebble(WatchConnection::EndpointFactorySettings, data);

    m_connection->writeToPebble(WatchConnection::EndpointVersion, QByteArray(1, 0));
}

void Pebble::onPebbleDisconnected()
{
    qDebug() << "Pebble disconnected:" << m_name;
    emit pebbleDisconnected();
}

void Pebble::profileSwitchRequired()
{
    QString *targetProfile = m_connection->isConnected()?&m_profileWhenConnected:&m_profileWhenDisconnected;
    if (targetProfile->isEmpty()) return;
    qDebug() << "Request Profile Switch: connected=" << m_connection->isConnected() << " profile=" << targetProfile;
     Core::instance()->platform()->setProfile(*targetProfile);
}

void Pebble::pebbleVersionReceived(const QByteArray &data)
{
    WatchDataReader wd(data);

    wd.skip(1);
    m_softwareBuildTime = QDateTime::fromTime_t(wd.read<quint32>());
    qDebug() << "Software Version build:" << m_softwareBuildTime;
    m_softwareVersion = wd.readFixedString(32);
    qDebug() << "Software Version string:" << m_softwareVersion;
    m_softwareCommitRevision = wd.readFixedString(8);
    qDebug() << "Software Version commit:" << m_softwareCommitRevision;

    m_recovery = wd.read<quint8>();
    qDebug() << "Recovery:" << m_recovery;
    HardwareRevision rev = (HardwareRevision)wd.read<quint8>();
    setHardwareRevision(rev);
    qDebug() << "HW Revision:" << rev;
    qDebug() << "Metadata Version:" << wd.read<quint8>();

    qDebug() << "Safe build:" << QDateTime::fromTime_t(wd.read<quint32>());
    qDebug() << "Safe version:" << wd.readFixedString(32);
    qDebug() << "safe commit:" << wd.readFixedString(8);
    qDebug() << "Safe recovery:" << wd.read<quint8>();
    qDebug() << "HW Revision:" << wd.read<quint8>();
    qDebug() << "Metadata Version:" << wd.read<quint8>();

    qDebug() << "BootloaderBuild" << QDateTime::fromTime_t(wd.read<quint32>());
    qDebug() << "hardwareRevision" << wd.readFixedString(9);
    m_serialNumber = wd.readFixedString(12);
    qDebug() << "serialnumber" << m_serialNumber;
    qDebug() << "BT address" << wd.readBytes(6).toHex();
    qDebug() << "CRC:" << wd.read<quint32>();
    qDebug() << "Resource timestamp:" << QDateTime::fromTime_t(wd.read<quint32>());
    m_language = wd.readFixedString(6);
    qDebug() << "Language" << m_language;
    m_langVer = wd.read<quint16>();
    qDebug() << "Language version" << m_langVer;
    // Capabilities is 64 bits but QFlags can only do 32 bits. lets split it into 2 * 32.
    // only 8 bits are used atm anyways.
    m_capabilities = QFlag(wd.readLE<quint32>());
    qDebug() << "Capabilities" << QString::number(m_capabilities, 16);
    qDebug() << "Capabilities" << wd.readLE<quint32>();
    m_isUnfaithful = wd.read<quint8>();
    qDebug() << "Is Unfaithful" << m_isUnfaithful;

    // This is useful for debugging
//    m_isUnfaithful = true;

    if (!m_recovery) {
        m_appManager->rescan();

        QSettings version(m_storagePath + "/watchinfo.conf", QSettings::IniFormat);
        if (version.value("syncedWithVersion").toString() != QStringLiteral(VERSION)) {
            m_isUnfaithful = true;
        }

        if (m_isUnfaithful) {
            qDebug() << "Pebble sync state unclear. Resetting Pebble watch.";
            resetPebble();
        } else {
            syncCalendar();
            syncApps();
            m_blobDB->insert(BlobDB::BlobDBIdAppSettings,m_healthParams);
            m_blobDB->setUnits(m_imperialUnits);
            m_timelineSync->syncLocker();
        }
        version.setValue("syncedWithVersion", QStringLiteral(VERSION));

        syncTime();
    }

    m_firmwareDownloader->checkForNewFirmware();
    emit pebbleConnected();

}

void Pebble::factorySettingsReceived(const QByteArray &data)
{
    qDebug() << "have factory settings" << data.toHex();

    WatchDataReader reader(data);
    quint8 status = reader.read<quint8>();
    quint8 len = reader.read<quint8>();

    if (status != 0x01 && len != 0x04) {
        qWarning() << "Unexpected data reading factory settings";
        return;
    }
    m_model = (Model)reader.read<quint32>();
    QSettings s(m_storagePath + "/watchinfo.conf", QSettings::IniFormat);
    s.setValue("watchModel", m_model);
}

void Pebble::phoneVersionAsked(const QByteArray &data)
{
    Q_UNUSED(data);
    QByteArray res;

    // android sends 09af
    Capabilities sessionCap(CapabilityAppRunState | CapabilityInfiniteLogDumping
                            | CapabilityUpdatedMusicProtocol | CapabilityExtendedNotifications
                            | /*CapabilityLanguagePacks |*/ Capability8kAppMessages
                            | /*CapabilityHealth |*/ CapabilityVoice
                            | CapabilityWeather /*| CapabilityXXX*/
                            | /*CapabilityYYY |*/ CapabilitySendSMS
                            );

    quint32 platformFlags = 16 | 32 | OSAndroid;

    WatchDataWriter writer(&res);
    writer.writeLE<quint8>(0x01); // ok
    writer.writeLE<quint32>(0xFFFFFFFF); // deprecated since 3.0
    writer.writeLE<quint32>(0); // deprecated since 3.0
    writer.write<quint32>(platformFlags);
    writer.write<quint8>(2); // response version
    writer.write<quint8>(3); // major version
    writer.write<quint8>(13); // minor version
    writer.write<quint8>(0); // bugfix version
    writer.writeLE<quint64>(sessionCap);

    qDebug() << "sending phone version" << res.toHex();

    m_connection->writeToPebble(WatchConnection::EndpointPhoneVersion, res);
}

void Pebble::appDownloadFinished(const QString &id)
{
    QUuid uuid = m_appManager->scanApp(m_storagePath + "/apps/" + id);
    if (uuid.isNull()) {
        qWarning() << "Error scanning downloaded app. Won't install on watch";
        return;
    }
    // Stop running pebble app to avoid race-condition with JSkit stop
    if (m_jskitManager->currentApp().uuid() == uuid) {
        m_appMsgManager->closeApp(uuid);
    }
    // Force app replacement to allow update from store/sdk
    m_appManager->insertAppMetaData(uuid,true);
    // The app will be re-launched here anyway
    m_pendingInstallations.append(uuid);
}

void Pebble::appInstalled(const QUuid &uuid) {
    if (m_pendingInstallations.contains(uuid)) {
        m_appMsgManager->launchApp(uuid);
    }

    if (uuid == m_lastSyncedAppUuid) {
        m_lastSyncedAppUuid = QUuid();

        m_appManager->setAppOrder(m_appManager->appUuids());
        QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
        if (settings.contains("watchface")) {
            m_appMsgManager->launchApp(settings.value("watchface").toUuid());
        }
    }
    m_timelineSync->syncLocker();
}

void Pebble::appStarted(const QUuid &uuid)
{
    AppInfo info = m_appManager->info(uuid);
    if (info.isWatchface()) {
        QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
        settings.setValue("watchface", uuid.toString());
    } else if(uuid == WeatherApp::appUUID && m_weatherProv != nullptr) {
        m_weatherProv->refreshWeather();
    }
}

void Pebble::onAppButtonPressed(const QString &uuid, const int &key)
{
    emit appButtonPressed(uuid, key);
}

void Pebble::muteNotificationSource(const QString &source)
{
    qDebug() << "Request to mute" << source;
    setNotificationFilter(source, NotificationDisabled);
}

void Pebble::resetTimeline()
{
    clearTimeline();
    syncCalendar();
    emit m_timelineSync->syncUrlChanged("");
}

void Pebble::resetPebble()
{
    resetTimeline();

    clearAppDB();
    syncApps();
}

void Pebble::syncApps()
{
    foreach (const QUuid &appUuid, m_appManager->appUuids()) {
        if (!m_appManager->info(appUuid).isSystemApp()) {
            qDebug() << "Inserting app" << m_appManager->info(appUuid).shortName() << "into BlobDB";
            m_appManager->insertAppMetaData(appUuid);
            m_lastSyncedAppUuid = appUuid;
        }
    }
}

void Pebble::syncTime()
{
    TimeMessage msg(TimeMessage::TimeOperationSetUTC);
    qDebug() << "Syncing Time" << QDateTime::currentDateTime() << msg.serialize().toHex();
    m_connection->writeToPebble(WatchConnection::EndpointTime, msg.serialize());
}

void Pebble::slotUpdateAvailableChanged()
{
    qDebug() << "update available" << m_firmwareDownloader->updateAvailable() << m_firmwareDownloader->candidateVersion();
    if (m_firmwareDownloader->updateAvailable()) {
        QJsonObject pin;
        pin.insert("id",QString("PebbleFirmware.%1").arg(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()));
        pin.insert("type",QString("notification"));
        pin.insert("source",QString("Pebble Firmware Updates"));
        pin.insert("dataSource",QString("PebbleFirmware:%1").arg(PlatformInterface::SysID));
        QJsonObject layout;
        layout.insert("title", QString("Pebble firmware %1 available").arg(m_firmwareDownloader->candidateVersion()));
        layout.insert("body",m_firmwareDownloader->releaseNotes());
        layout.insert("type",QString("genericNotification"));
        layout.insert("tinyIcon",QString("system://images/NOTIFICATION_FLAG"));
        pin.insert("layout",layout);
        insertPin(pin);

        m_connection->systemMessage(WatchConnection::SystemMessageFirmwareAvailable);
    }
    emit updateAvailableChanged();
}


TimeMessage::TimeMessage(TimeMessage::TimeOperation operation) :
    m_operation(operation)
{

}
QByteArray TimeMessage::serialize() const
{
    QByteArray ret;
    WatchDataWriter writer(&ret);
    writer.write<quint8>(m_operation);
    switch (m_operation) {
    case TimeOperationSetLocaltime:
        writer.writeLE<quint32>(QDateTime::currentMSecsSinceEpoch() / 1000);
        break;
    case TimeOperationSetUTC:
        writer.write<quint32>(QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000);
        writer.write<qint16>(QDateTime::currentDateTime().offsetFromUtc() / 60);
        writer.writePascalString(QDateTime::currentDateTime().timeZone().displayName(QTimeZone::StandardTime));
        break;
    default:
        ;
    }
    return ret;
}
