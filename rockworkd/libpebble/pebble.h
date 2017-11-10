#ifndef PEBBLE_H
#define PEBBLE_H

#include "musicmetadata.h"
#include "appinfo.h"
#include "healthparams.h"

#include <QObject>
#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>
#include <QDateTime>

class WatchConnection;
class MusicEndpoint;
class PhoneCallEndpoint;
class AppGlances;
class AppManager;
class AppMsgManager;
class BankManager;
class JSKitManager;
class BlobDB;
class AppDownloader;
class ScreenshotEndpoint;
class FirmwareDownloader;
class WatchLogEndpoint;
class DataLoggingEndpoint;
class DevConnection;
class TimelineManager;
class TimelineSync;
class SendTextApp;
class WeatherApp;
class WeatherProvider;
class VoiceEndpoint;
struct SpeexInfo;
struct AudioStream;

class QNetworkAccessManager;
class QSettings;

class Pebble : public QObject
{
    Q_OBJECT
    Q_ENUMS(Pebble::NotificationType)
    Q_PROPERTY(QBluetoothAddress address MEMBER m_address)
    Q_PROPERTY(QString name MEMBER m_name)
    Q_PROPERTY(HardwareRevision HardwareRevision READ hardwareRevision)
    Q_PROPERTY(Model model READ model)
    Q_PROPERTY(HardwarePlatform hardwarePlatform MEMBER m_hardwarePlatform)
    Q_PROPERTY(QString softwareVersion MEMBER m_softwareVersion)
    Q_PROPERTY(QString serialNumber MEMBER m_serialNumber)
    Q_PROPERTY(QString language MEMBER m_language)

public:
    explicit Pebble(const QBluetoothAddress &address, QObject *parent = 0);

    QBluetoothAddress address() const;

    QString name() const;
    void setName(const QString &name);

    QBluetoothLocalDevice::Pairing pairingStatus() const;

    bool connected() const;
    void connect();
    BlobDB *blobdb() const;
    TimelineSync *tlSync() const;
    TimelineManager *timeline() const;
    AppGlances *appGlances() const;

    QDateTime softwareBuildTime() const;
    QString softwareVersion() const;
    QString softwareCommitRevision() const;
    HardwareRevision hardwareRevision() const;
    Model model() const;
    HardwarePlatform hardwarePlatform() const;
    QString platformString() const;
    QString platformName() const;
    QString serialNumber() const;
    QString language() const;
    quint16 langVer() const;
    Capabilities capabilities() const;
    bool isUnfaithful() const;
    bool recovery() const;

    QString storagePath() const;
    QString imagePath() const;
    enum NotificationFilter {
        NotificationForgotten = -1,
        NotificationDisabled = 0,
        NotificationDisabledActive = 1,
        NotificationEnabled = 2
    };
    DevConnection * devConnection();

    bool syncAppsFromCloud() const;
    const QString oauthToken() const;
    const QString accountName() const;
    const QString accountEmail() const;
    QNetworkAccessManager *nam() const;
    QVariantMap cannedMessages() const;
    void setCannedMessages(const QVariantMap &cans) const;
    QHash<QString,QStringList> getCannedMessages(const QStringList &groups = QStringList()) const;
    void setCannedContacts(const QMap<QString,QStringList> &cans, bool push=true);
    QMap<QString,QStringList> getCannedContacts(const QStringList &names = QStringList()) const;
    qint32 timelineWindowStart() const;
    qint32 timelineWindowFade() const;
    qint32 timelineWindowEnd() const;

public slots:
    void setTimelineWindow(qint32 start, qint32 fade, qint32 end);
    void setOAuthToken(const QString &token);
    void setSyncAppsFromCloud(bool enable);

    void setWeatherApiKey(const QString &key);
    void setWeatherAltKey(const QString &key);
    QString getWeatherAltKey() const;
    void setWeatherLanguage(const QString &lang);
    QString getWeatherLanguage() const;
    void setWeatherUnits(const QString &u);
    QString getWeatherUnits() const;
    void setWeatherLocations(const QVariantList &locations);
    QVariantList getWeatherLocations() const;
    void injectWeatherConditions(const QString &location, const QVariantMap &conditions);

    QVariantMap notificationsFilter() const;
    void setNotificationFilter(const QString &sourceId, const QString &name, const QString &icon, const NotificationFilter enabled);
    void setNotificationFilter(const QString &sourceId, const NotificationFilter enabled);
    void forgetNotificationFilter(const QString &sourceId);
    QString findNotificationData(const QString &sourceId, const QString &key);
    void insertPin(const QJsonObject &json);
    void removePin(const QString &guid);

    void setDevConListenPort(quint16 port);
    void setDevConCloudEnabled(bool enabled);
    void setDevConLogLevel(int level);
    void setDevConLogDump(bool enable);

    void clearTimeline();
    void syncCalendar();
    void resetTimeline();
    void setCalendarSyncEnabled(bool enabled);
    bool calendarSyncEnabled() const;

    void clearAppDB();
    void installApp(const QString &id);
    void sideloadApp(const QString &packageFile);
    QList<QUuid> installedAppIds();
    void setAppOrder(const QList<QUuid> &newList);
    AppInfo appInfo(const QUuid &uuid);
    void removeApp(const QUuid &uuid);
    AppInfo currentApp();

    void closeApp(const QUuid &uuid);    
    void sendAppData(const QUuid &uuid, const QVariantMap &data);
    void launchApp(const QUuid &uuid);

    void requestConfigurationURL(const QUuid &uuid);
    void configurationClosed(const QUuid &uuid, const QString &result);

    void requestScreenshot();
    QStringList screenshots() const;
    void removeScreenshot(const QString &filename);

    bool firmwareUpdateAvailable() const;
    QString candidateFirmwareVersion() const;
    QString firmwareReleaseNotes() const;
    void upgradeFirmware() const;
    bool upgradingFirmware() const;
    void loadLanguagePack(const QString &pblFile) const;

    void setHealthParams(const HealthParams &healthParams);
    HealthParams healthParams() const;

    void setImperialUnits(bool imperial);
    bool imperialUnits() const;

    void setProfileWhen(const bool connected, const QString &profile);
    QString profileWhen(bool connected) const;

    void dumpLogs(const QString &fileName) const;
    //void voiceSessionResponse(quint8 result, const QUuid &appUuid);
    void voiceAudioStop();
    void voiceSessionResult(const QString &fileName, const QVariantList &sentences);

private slots:
    void onPebbleConnected();
    void onPebbleDisconnected();
    void profileSwitchRequired();
    void pebbleVersionReceived(const QByteArray &data);
    void factorySettingsReceived(const QByteArray &data);
    void phoneVersionAsked(const QByteArray &data);
    void appDownloadFinished(const QString &id);
    void appInstalled(const QUuid &uuid);
    void appStarted(const QUuid &uuid);
    void saveTextContacts() const;
    void saveTextMessages(const QByteArray &key) const;
    void muteNotificationSource(const QString &source);
    void voiceSessionRequest(const QUuid &appUuid, const SpeexInfo &codec);
    void voiceAudioStream(quint16 sid, const AudioStream &frames);
    void voiceSessionClose(quint16 sesId);
    void saveWeatherLocations() const;
    void initWeatherProvider(const QSettings &settings);

    void resetPebble();
    void syncApps();
    void syncTime();

    void slotUpdateAvailableChanged();

signals:
    void pebbleConnected();
    void pebbleDisconnected();
    void notificationFilterChanged(const QString &sourceId, const QString &name, const QString &icon, const NotificationFilter enabled);
    void musicControlPressed(MusicControlButton control);
    void installedAppsChanged();
    void openURL(const QString &uuid, const QString &url);
    void screenshotAdded(const QString &filename);
    void screenshotRemoved(const QString &filename);
    void updateAvailableChanged();
    void upgradingFirmwareChanged();
    void languagePackChanged();
    void logsDumped(bool success);
    void contactsChanged() const;
    void messagesChanged() const;
    void weatherLocationsChanged(const QVariantList &locations) const;
    void voiceSessionSetup(const QString &fileName, const QString &format, const QString &appUuid);
    void voiceSessionStream(const QString &fileName);
    void voiceSessionDumped(const QString &fileName);
    void voiceSessionClosed(const QString &fileName);

    void calendarSyncEnabledChanged();
    void imperialUnitsChanged();
    void profileConnectionSwitchChanged(bool connected);
    void healtParamsChanged();
    void devConServerStateChanged(bool state);
    void devConCloudStateChanged(bool state);
    void oauthTokenChanged(const QString &token);
private:
    void setHardwareRevision(HardwareRevision hardwareRevision);

    QBluetoothAddress m_address;
    QString m_name;
    QDateTime m_softwareBuildTime;
    QString m_softwareVersion;
    QString m_softwareCommitRevision;
    HardwareRevision m_hardwareRevision;
    HardwarePlatform m_hardwarePlatform = HardwarePlatformUnknown;
    Model m_model = ModelUnknown;
    QString m_serialNumber;
    QString m_language;
    quint16 m_langVer;
    Capabilities m_capabilities = CapabilityNone;
    bool m_isUnfaithful = false;
    bool m_recovery = false;

    WatchConnection *m_connection;
    MusicEndpoint *m_musicEndpoint;
    PhoneCallEndpoint *m_phoneCallEndpoint;
    AppGlances *m_appGlances;
    AppManager *m_appManager;
    AppMsgManager *m_appMsgManager;
    JSKitManager *m_jskitManager;
    BankManager *m_bankManager;
    BlobDB *m_blobDB;
    AppDownloader *m_appDownloader;
    ScreenshotEndpoint *m_screenshotEndpoint;
    FirmwareDownloader *m_firmwareDownloader;
    WatchLogEndpoint *m_logEndpoint;
    DataLoggingEndpoint *m_dataLogEndpoint;
    SendTextApp * m_sendTextApp;
    WeatherApp * m_weatherApp;
    WeatherProvider * m_weatherProv;
    VoiceEndpoint * m_voiceEndpoint;
    QTemporaryFile* m_voiceSessDump = nullptr;

    QString m_storagePath;
    QString m_imagePath;
    QList<QUuid> m_pendingInstallations;
    QUuid m_lastSyncedAppUuid;

    bool m_calendarSyncEnabled = true;
    QString m_profileWhenConnected = "ignore";
    QString m_profileWhenDisconnected = "ignore";
    HealthParams m_healthParams;
    bool m_imperialUnits = false;
    DevConnection *m_devConnection;
    TimelineManager *m_timelineManager;
    TimelineSync *m_timelineSync;
    QNetworkAccessManager *m_nam;
};

/*
  Capabilities received from phone:
  In order, starting at zero, in little-endian (unlike the rest of the messsage), the bits sent by the watch indicate support for:
  - app run state,
  - infinite log dumping,
  - updated music protocol,
  - extended notification service,
  - language packs,
  - 8k app messages,
  - health,
  - voice

  The capability bits sent *to* the watch are, starting at zero:
  - app run state,
  - infinite log dumping,
  - updated music service,
  - extended notification service,
  - (unused),
  - 8k app messages,
  - (unused),
  - third-party voice
  */



class TimeMessage: public PebblePacket
{
public:
    enum TimeOperation {
        TimeOperationGetRequest = 0x00,
        TimeOperationGetResponse = 0x01,
        TimeOperationSetLocaltime = 0x02,
        TimeOperationSetUTC = 0x03
    };
    TimeMessage(TimeOperation operation);

    QByteArray serialize() const override;

private:
    TimeOperation m_operation = TimeOperationGetRequest;
};

#endif // PEBBLE_H
