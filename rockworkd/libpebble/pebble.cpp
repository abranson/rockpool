#include "pebble.h"
#include "watchconnection.h"
#include "notificationendpoint.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"
#include "musicendpoint.h"
#include "phonecallendpoint.h"
#include "appmanager.h"
#include "appmsgmanager.h"
#include "jskit/jskitmanager.h"
#include "blobdb.h"
#include "appdownloader.h"
#include "screenshotendpoint.h"
#include "firmwaredownloader.h"
#include "watchlogendpoint.h"
#include "core.h"
#include "platforminterface.h"
#include "ziphelper.h"
#include "dataloggingendpoint.h"

#include "QDir"
#include <QDateTime>
#include <QStandardPaths>
#include <QSettings>
#include <QTimeZone>

Pebble::Pebble(const QBluetoothAddress &address, QObject *parent):
    QObject(parent),
    m_address(address)
{
    m_storagePath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/" + m_address.toString().replace(':', '_') + "/";

    m_connection = new WatchConnection(this);
    QObject::connect(m_connection, &WatchConnection::watchConnected, this, &Pebble::onPebbleConnected);
    QObject::connect(m_connection, &WatchConnection::watchDisconnected, this, &Pebble::onPebbleDisconnected);

    m_connection->registerEndpointHandler(WatchConnection::EndpointVersion, this, "pebbleVersionReceived");
    m_connection->registerEndpointHandler(WatchConnection::EndpointPhoneVersion, this, "phoneVersionAsked");
    m_connection->registerEndpointHandler(WatchConnection::EndpointFactorySettings, this, "factorySettingsReceived");

    m_dataLogEndpoint = new DataLoggingEndpoint(this, m_connection);

    m_notificationEndpoint = new NotificationEndpoint(this, m_connection);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::notificationReceived, this, &Pebble::sendNotification);

    m_musicEndpoint = new MusicEndpoint(this, m_connection);
    m_musicEndpoint->setMusicMetadata(Core::instance()->platform()->musicMetaData());
    QObject::connect(m_musicEndpoint, &MusicEndpoint::musicControlPressed, Core::instance()->platform(), &PlatformInterface::sendMusicControlCommand);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::musicMetadataChanged, m_musicEndpoint, &MusicEndpoint::setMusicMetadata);

    m_phoneCallEndpoint = new PhoneCallEndpoint(this, m_connection);
    QObject::connect(m_phoneCallEndpoint, &PhoneCallEndpoint::hangupCall, Core::instance()->platform(), &PlatformInterface::hangupCall);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::incomingCall, m_phoneCallEndpoint, &PhoneCallEndpoint::incomingCall);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::callStarted, m_phoneCallEndpoint, &PhoneCallEndpoint::callStarted);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::callEnded, m_phoneCallEndpoint, &PhoneCallEndpoint::callEnded);

    m_appManager = new AppManager(this, m_connection);
    QObject::connect(m_appManager, &AppManager::appsChanged, this, &Pebble::installedAppsChanged);
    QObject::connect(m_appManager, &AppManager::idMismatchDetected, this, &Pebble::resetPebble);

    m_appMsgManager = new AppMsgManager(this, m_appManager, m_connection);
    m_jskitManager = new JSKitManager(this, m_connection, m_appManager, m_appMsgManager, this);
    QObject::connect(m_jskitManager, &JSKitManager::openURL, this, &Pebble::openURL);
    QObject::connect(m_appMsgManager, &AppMsgManager::appStarted, this, &Pebble::appStarted);

    m_blobDB = new BlobDB(this, m_connection);
    QObject::connect(m_blobDB, &BlobDB::muteSource, this, &Pebble::muteNotificationSource);
    QObject::connect(m_blobDB, &BlobDB::actionTriggered, Core::instance()->platform(), &PlatformInterface::actionTriggered);
    QObject::connect(m_blobDB, &BlobDB::appInserted, this, &Pebble::appInstalled);
    QObject::connect(Core::instance()->platform(), &PlatformInterface::organizerItemsChanged, this, &Pebble::syncCalendar);

    m_appDownloader = new AppDownloader(m_storagePath, this);
    QObject::connect(m_appDownloader, &AppDownloader::downloadFinished, this, &Pebble::appDownloadFinished);

    m_screenshotEndpoint = new ScreenshotEndpoint(this, m_connection, this);
    QObject::connect(m_screenshotEndpoint, &ScreenshotEndpoint::screenshotAdded, this, &Pebble::screenshotAdded);
    QObject::connect(m_screenshotEndpoint, &ScreenshotEndpoint::screenshotRemoved, this, &Pebble::screenshotRemoved);

    m_firmwareDownloader = new FirmwareDownloader(this, m_connection);
    QObject::connect(m_firmwareDownloader, &FirmwareDownloader::updateAvailableChanged, this, &Pebble::slotUpdateAvailableChanged);
    QObject::connect(m_firmwareDownloader, &FirmwareDownloader::upgradingChanged, this, &Pebble::upgradingFirmwareChanged);

    m_logEndpoint = new WatchLogEndpoint(this, m_connection);
    QObject::connect(m_logEndpoint, &WatchLogEndpoint::logsFetched, this, &Pebble::logsDumped);

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
    case HardwareRevisionUNKNOWN:
        m_hardwarePlatform = HardwarePlatformUnknown;
        break;
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
    }
}

HardwarePlatform Pebble::hardwarePlatform() const
{
    return m_hardwarePlatform;
}

QString Pebble::serialNumber() const
{
    return m_serialNumber;
}

QString Pebble::language() const
{
    return m_language;
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

void Pebble::setHealthParams(const HealthParams &healthParams)
{
    m_healthParams = healthParams;
    m_blobDB->setHealthParams(healthParams);
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

void Pebble::dumpLogs(const QString &fileName) const
{
    m_logEndpoint->fetchLogs(fileName);
}

QString Pebble::storagePath() const
{
    return m_storagePath;
}

QHash<QString, bool> Pebble::notificationsFilter() const
{
    QHash<QString, bool> ret;
    QString settingsFile = m_storagePath + "/notifications.conf";
    QSettings s(settingsFile, QSettings::IniFormat);
    foreach (const QString &key, s.allKeys()) {
        ret.insert(key, s.value(key).toBool());
    }
    return ret;
}

void Pebble::setNotificationFilter(const QString &sourceId, bool enabled)
{
    QString settingsFile = m_storagePath + "/notifications.conf";
    QSettings s(settingsFile, QSettings::IniFormat);
    if (!s.contains(sourceId) || s.value(sourceId).toBool() != enabled) {
        s.setValue(sourceId, enabled);
        emit notificationFilterChanged(sourceId, enabled);
    }
}

void Pebble::sendNotification(const Notification &notification)
{
    if (!notificationsFilter().value(notification.sourceId(), true)) {
        qDebug() << "Notifications for" << notification.sourceId() << "disabled.";
        return;
    }
    // In case it wasn't there before, make sure to write it to the config now so it will appear in the config app.
    setNotificationFilter(notification.sourceId(), true);

    qDebug() << "Sending notification from source" << notification.sourceId() << "to watch";

    if (m_softwareVersion < "v3.0") {
        m_notificationEndpoint->sendLegacyNotification(notification);
    } else {
        m_blobDB->insertNotification(notification);
    }
}

void Pebble::clearAppDB()
{
    m_blobDB->clearApps();
}

void Pebble::clearTimeline()
{
    m_blobDB->clearTimeline();
}

void Pebble::setCalendarSyncEnabled(bool enabled)
{
    if (m_calendarSyncEnabled == enabled) {
        return;
    }
    m_calendarSyncEnabled = enabled;
    emit calendarSyncEnabledChanged();

    if (!m_calendarSyncEnabled) {
        m_blobDB->clearTimeline();
    } else {
        syncCalendar(Core::instance()->platform()->organizerItems());
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

void Pebble::syncCalendar(const QList<CalendarEvent> &items)
{
    if (connected() && m_calendarSyncEnabled) {
        m_blobDB->syncCalendar(items);
    }
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
    int i = 0;
    do {
        QDir dir(m_storagePath + "/apps/sideload" + QString::number(i));
        if (!dir.exists()) {
            if (!dir.mkpath(dir.absolutePath())) {
                qWarning() << "Error creating dir for unpacking. Cannot install package" << packageFile;
                return;
            }
            id = "sideload" + QString::number(i);
        }
        i++;
    } while (id.isEmpty());

    if (!ZipHelper::unpackArchive(targetFile, m_storagePath + "/apps/" + id)) {
        qWarning() << "Error unpacking App zip file" << targetFile << "to" << m_storagePath + "/apps/" + id;
        return;
    }

    qDebug() << "Sideload package unpacked.";
    appDownloadFinished(id);
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

void Pebble::removeApp(const QUuid &uuid)
{
    qDebug() << "Should remove app:" << uuid;
    m_blobDB->removeApp(m_appManager->info(uuid));
    m_appManager->removeApp(uuid);
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
    qDebug() << "Language version" << wd.read<quint16>();
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
            syncCalendar(Core::instance()->platform()->organizerItems());
            syncApps();
            m_blobDB->setHealthParams(m_healthParams);
            m_blobDB->setUnits(m_imperialUnits);
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

    QByteArray res;

    Capabilities sessionCap(CapabilityHealth
                            | CapabilityAppRunState
                            | CapabilityUpdatedMusicProtocol | CapabilityInfiniteLogDumping | Capability8kAppMessages);

    quint32 platformFlags = 16 | 32 | OSAndroid;

    WatchDataWriter writer(&res);
    writer.writeLE<quint8>(0x01); // ok
    writer.writeLE<quint32>(0xFFFFFFFF);
    writer.writeLE<quint32>(sessionCap);
    writer.write<quint32>(platformFlags);
    writer.write<quint8>(2); // response version
    writer.write<quint8>(3); // major version
    writer.write<quint8>(0); // minor version
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
    m_blobDB->insertAppMetaData(m_appManager->info(uuid));
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
}

void Pebble::appStarted(const QUuid &uuid)
{
    AppInfo info = m_appManager->info(uuid);
    if (info.isWatchface()) {
        QSettings settings(m_storagePath + "/appsettings.conf", QSettings::IniFormat);
        settings.setValue("watchface", uuid.toString());
    }
}

void Pebble::muteNotificationSource(const QString &source)
{
    setNotificationFilter(source, false);
}

void Pebble::resetPebble()
{
    clearTimeline();
    syncCalendar(Core::instance()->platform()->organizerItems());

    clearAppDB();
    syncApps();
}

void Pebble::syncApps()
{
    QUuid lastSyncedAppUuid;
    foreach (const QUuid &appUuid, m_appManager->appUuids()) {
        if (!m_appManager->info(appUuid).isSystemApp()) {
            qDebug() << "Inserting app" << m_appManager->info(appUuid).shortName() << "into BlobDB";
            m_blobDB->insertAppMetaData(m_appManager->info(appUuid));
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
