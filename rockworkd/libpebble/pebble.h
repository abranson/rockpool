#ifndef PEBBLE_H
#define PEBBLE_H

#include "musicmetadata.h"
#include "notification.h"
#include "calendarevent.h"
#include "appinfo.h"
#include "healthparams.h"

#include <QObject>
#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>
#include <QTimer>

class WatchConnection;
class NotificationEndpoint;
class MusicEndpoint;
class PhoneCallEndpoint;
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

    QDateTime softwareBuildTime() const;
    QString softwareVersion() const;
    QString softwareCommitRevision() const;
    HardwareRevision hardwareRevision() const;
    Model model() const;
    HardwarePlatform hardwarePlatform() const;
    QString serialNumber() const;
    QString language() const;
    Capabilities capabilities() const;
    bool isUnfaithful() const;
    bool recovery() const;

    QString storagePath() const;

public slots:
    QHash<QString, bool> notificationsFilter() const;
    void setNotificationFilter(const QString &sourceId, bool enabled);
    void sendNotification(const Notification &notification);

    void clearTimeline();
    void setCalendarSyncEnabled(bool enabled);
    bool calendarSyncEnabled() const;

    void clearAppDB();
    void installApp(const QString &id);
    void sideloadApp(const QString &packageFile);
    QList<QUuid> installedAppIds();
    void setAppOrder(const QList<QUuid> &newList);
    AppInfo appInfo(const QUuid &uuid);
    void removeApp(const QUuid &uuid);

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

    void setHealthParams(const HealthParams &healthParams);
    HealthParams healthParams() const;

    void setImperialUnits(bool imperial);
    bool imperialUnits() const;

    void dumpLogs(const QString &fileName) const;

private slots:
    void onPebbleConnected();
    void onPebbleDisconnected();
    void pebbleVersionReceived(const QByteArray &data);
    void factorySettingsReceived(const QByteArray &data);
    void phoneVersionAsked(const QByteArray &data);
    void appDownloadFinished(const QString &id);
    void appInstalled(const QUuid &uuid);
    void appStarted(const QUuid &uuid);
    void muteNotificationSource(const QString &source);

    void resetPebble();
    void syncApps();
    void syncTime();
    void syncCalendar(const QList<CalendarEvent> &items);

    void slotUpdateAvailableChanged();

signals:
    void pebbleConnected();
    void pebbleDisconnected();
    void notificationFilterChanged(const QString &sourceId, bool enabled);
    void musicControlPressed(MusicControlButton control);
    void installedAppsChanged();
    void openURL(const QString &uuid, const QString &url);
    void screenshotAdded(const QString &filename);
    void screenshotRemoved(const QString &filename);
    void updateAvailableChanged();
    void upgradingFirmwareChanged();
    void logsDumped(bool success);

    void calendarSyncEnabledChanged();
    void imperialUnitsChanged();
    void healtParamsChanged();
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
    Capabilities m_capabilities = CapabilityNone;
    bool m_isUnfaithful = false;
    bool m_recovery = false;

    WatchConnection *m_connection;
    NotificationEndpoint *m_notificationEndpoint;
    MusicEndpoint *m_musicEndpoint;
    PhoneCallEndpoint *m_phoneCallEndpoint;
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

    QString m_storagePath;
    QList<QUuid> m_pendingInstallations;
    QUuid m_lastSyncedAppUuid;

    bool m_calendarSyncEnabled = true;
    HealthParams m_healthParams;
    bool m_imperialUnits = false;
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
