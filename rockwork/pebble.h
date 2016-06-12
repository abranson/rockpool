#ifndef PEBBLE_H
#define PEBBLE_H

#include <QObject>
#include <QDBusInterface>

class NotificationSourceModel;
class ApplicationsModel;
class ScreenshotModel;

class Pebble : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString hardwarePlatform READ hardwarePlatform NOTIFY hardwarePlatformChanged)
    Q_PROPERTY(int model READ model NOTIFY modelChanged)
    Q_PROPERTY(NotificationSourceModel* notifications READ notifications CONSTANT)
    Q_PROPERTY(ApplicationsModel* installedApps READ installedApps CONSTANT)
    Q_PROPERTY(ApplicationsModel* installedWatchfaces READ installedWatchfaces CONSTANT)
    Q_PROPERTY(ScreenshotModel* screenshots READ screenshots CONSTANT)
    Q_PROPERTY(bool recovery READ recovery NOTIFY connectedChanged)
    Q_PROPERTY(QString softwareVersion READ softwareVersion NOTIFY connectedChanged)
    Q_PROPERTY(bool firmwareUpgradeAvailable READ firmwareUpgradeAvailable NOTIFY firmwareUpgradeAvailableChanged)
    Q_PROPERTY(QString firmwareReleaseNotes READ firmwareReleaseNotes NOTIFY firmwareUpgradeAvailableChanged)
    Q_PROPERTY(QString candidateVersion READ candidateVersion NOTIFY firmwareUpgradeAvailableChanged)
    Q_PROPERTY(bool upgradingFirmware READ upgradingFirmware NOTIFY upgradingFirmwareChanged)
    Q_PROPERTY(QVariantMap healthParams READ healthParams WRITE setHealthParams NOTIFY healthParamsChanged)
    Q_PROPERTY(bool imperialUnits READ imperialUnits WRITE setImperialUnits NOTIFY imperialUnitsChanged)
    Q_PROPERTY(QString profileWhenConnected READ profileWhenConnected WRITE setProfileWhenConnected NOTIFY profileWhenConnectedChanged)
    Q_PROPERTY(QString profileWhenDisconnected READ profileWhenDisconnected WRITE setProfileWhenDisconnected NOTIFY profileWhenDisconnectedChanged)
    Q_PROPERTY(bool calendarSyncEnabled READ calendarSyncEnabled WRITE setCalendarSyncEnabled NOTIFY calendarSyncEnabledChanged)
    Q_PROPERTY(bool devConnEnabled READ devConnEnabled WRITE setDevConnEnabled NOTIFY devConnEnabledChanged)
    Q_PROPERTY(bool devConnCloudEnabled READ devConnCloudEnabled WRITE setDevConnCloudEnabled NOTIFY devConnCloudEnabledChanged)
    Q_PROPERTY(quint16 devConListenPort READ devConListenPort WRITE setDevConListenPort NOTIFY devConListenPortChanged)
    Q_PROPERTY(bool devConnServerRunning READ devConnServerRunning NOTIFY devConnServerRunningChanged)
    Q_PROPERTY(bool devConCloudConnected READ devConCloudConnected NOTIFY devConCloudConnectedChanged)
    Q_PROPERTY(bool syncAppsFromCloud READ syncAppsFromCloud WRITE setSyncAppsFromCloud NOTIFY syncAppsFromCloudChanged)
    Q_PROPERTY(QString oauthToken READ oauthToken WRITE setOAuthToken NOTIFY oauthTokenChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountNameChanged)
    Q_PROPERTY(QString accountEmail READ accountEmail NOTIFY accountEmailChanged)
    Q_PROPERTY(int timelineWindowStart MEMBER m_timelienWindowStart)
    Q_PROPERTY(int timelineWindowFade MEMBER m_timelienWindowFade)
    Q_PROPERTY(int timelineWindowEnd MEMBER m_timelienWindowEnd)

public:
    explicit Pebble(const QDBusObjectPath &path, QObject *parent = 0);

    QDBusObjectPath path();

    bool connected() const;
    QString address() const;
    QString name() const;
    QString hardwarePlatform() const;
    QString serialNumber() const;
    QString softwareVersion() const;
    int model() const;
    bool recovery() const;
    bool upgradingFirmware() const;

    NotificationSourceModel *notifications() const;
    ApplicationsModel* installedApps() const;
    ApplicationsModel* installedWatchfaces() const;
    ScreenshotModel* screenshots() const;

    bool firmwareUpgradeAvailable() const;
    QString firmwareReleaseNotes() const;
    QString candidateVersion() const;

    QVariantMap healthParams() const;
    void setHealthParams(const QVariantMap &healthParams);

    bool imperialUnits() const;
    void setImperialUnits(bool imperialUnits);

    QString profileWhenConnected();
    void setProfileWhenConnected(const QString &profile);
    QString profileWhenDisconnected();
    void setProfileWhenDisconnected(const QString &profile);

    bool calendarSyncEnabled() const;
    void setCalendarSyncEnabled(bool enabled);

    bool devConnServerRunning() const;
    bool devConCloudConnected() const;
    bool devConnEnabled() const;
    bool devConnCloudEnabled() const;
    quint16 devConListenPort() const;
    QString oauthToken() const;
    QString accountName() const;
    QString accountEmail() const;
    bool syncAppsFromCloud() const;

public slots:
    void setNotificationFilter(const QString &sourceId, int enabled);
    void forgetNotificationFilter(const QString &sourceId);
    void removeApp(const QString &uuid);
    void installApp(const QString &storeId);
    void sideloadApp(const QString &packageFile);
    void moveApp(const QString &uuid, int toIndex);
    void requestConfigurationURL(const QString &uuid);
    void configurationClosed(const QString &uuid, const QString &url);
    void launchApp(const QString &uuid);
    void requestScreenshot();
    void removeScreenshot(const QString &filename);
    void performFirmwareUpgrade();
    void dumpLogs(const QString &filename);

    void setDevConnEnabled(bool enabled);
    void setDevConnCloudEnabled(bool enabled);
    void setDevConListenPort(quint16 port);
    void setOAuthToken(const QString &token);
    void setSyncAppsFromCloud(bool enable);
    void setTimelineWindow();

signals:
    void connectedChanged();
    void hardwarePlatformChanged();
    void modelChanged();
    void firmwareUpgradeAvailableChanged();
    void upgradingFirmwareChanged();
    void logsDumped(bool success);
    void healthParamsChanged();
    void imperialUnitsChanged();
    void profileWhenDisconnectedChanged();
    void profileWhenConnectedChanged();
    void calendarSyncEnabledChanged();
    void devConnEnabledChanged();
    void devConnCloudEnabledChanged();
    void devConListenPortChanged();
    void devConnServerRunningChanged();
    void devConCloudConnectedChanged();

    void oauthTokenChanged();
    void accountNameChanged();
    void accountEmailChanged();
    void syncAppsFromCloudChanged();

    void openURL(const QString &uuid, const QString &url);

private:
    QVariant fetchProperty(const QString &propertyName) const;

private slots:
    void dataChanged();
    void pebbleConnected();
    void pebbleDisconnected();
    void notificationFilterChanged(const QString &sourceId, const QString &name, const QString &icon, const int enabled);
    void refreshNotifications();
    void refreshApps();
    void appsSorted();
    void refreshScreenshots();
    void screenshotAdded(const QString &filename);
    void screenshotRemoved(const QString &filename);
    void refreshFirmwareUpdateInfo();
    void devConStateChanged(bool state);
    void devConCloudChanged(bool state);

private:
    QDBusObjectPath m_path;

    bool m_connected = false;
    QString m_address;
    QString m_name;
    QString m_hardwarePlatform;
    QString m_serialNumber;
    QString m_softwareVersion;
    bool m_recovery = false;
    int m_model = 0;
    QDBusInterface *m_iface;
    NotificationSourceModel *m_notifications;
    ApplicationsModel *m_installedApps;
    ApplicationsModel *m_installedWatchfaces;
    ScreenshotModel *m_screenshotModel;

    bool m_firmwareUpgradeAvailable = false;
    QString m_firmwareReleaseNotes;
    QString m_candidateVersion;
    bool m_upgradingFirmware = false;

    qint32 m_timelienWindowStart;
    qint32 m_timelienWindowFade;
    qint32 m_timelienWindowEnd;
};

#endif // PEBBLE_H
