#ifndef PEBBLE_H
#define PEBBLE_H

#include <QObject>
#include <QDBusAbstractInterface>
#include <QHash>
#include <QSet>

class NotificationSourceModel;
class ApplicationsModel;
class ScreenshotModel;
class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
class RockpoolAccount;

/** Static proxy base that avoids QDBusInterface's runtime introspection in the UI thread. */
class RockworkPebbleInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit RockworkPebbleInterface(const QString &path, QObject *parent = 0);

signals:
    void Connected();
    void Disconnected();
    void ConnectionStateChanged(int state);
    void InstalledAppsChanged();
    void OpenURL(const QString &uuid, const QString &url);
    void NotificationFilterChanged(const QString &sourceId, const QString &name,
                                   const QString &icon, int enabled);
    void ScreenshotAdded(const QString &filename);
    void ScreenshotRemoved(const QString &filename);
    void FirmwareUpgradeAvailableChanged();
    void LanguageVersionChanged();
    void UpgradingFirmwareChanged();
    void LogsDumped(bool success);
    void HealthParamsChanged();
    void HealthDataChanged();
    void ImperialUnitsChanged();
    void ProfileWhenConnectedChanged();
    void ProfileWhenDisconnectedChanged();
    void CalendarSyncEnabledChanged();
    void DevConnectionChanged(bool state);
    void WeatherLocationsChanged(const QVariantList &locations);
};

class Pebble : public QObject
{
    Q_OBJECT
    // hardware details
    Q_PROPERTY(QString name READ name NOTIFY identityChanged)
    Q_PROPERTY(QString address READ address NOTIFY identityChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    // Finer-grained than `connected`: 0=Disconnected 1=Connecting 2=Negotiating 3=Connected
    // 4=Failed. Lets the UI distinguish a reconnecting/failed watch from an idle one.
    Q_PROPERTY(int connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY connectionStateChanged)
    Q_PROPERTY(QString platformString READ platformString NOTIFY platformStringChanged)
    Q_PROPERTY(QString hardwarePlatform READ hardwarePlatform NOTIFY hardwarePlatformChanged)
    Q_PROPERTY(int model READ model NOTIFY modelChanged)
    // Firmware management
    Q_PROPERTY(bool recovery READ recovery NOTIFY recoveryChanged)
    Q_PROPERTY(QString softwareVersion READ softwareVersion NOTIFY softwareVersionChanged)
    Q_PROPERTY(bool firmwareUpgradeAvailable READ firmwareUpgradeAvailable NOTIFY firmwareUpgradeAvailableChanged)
    Q_PROPERTY(QString firmwareReleaseNotes READ firmwareReleaseNotes NOTIFY firmwareUpgradeAvailableChanged)
    Q_PROPERTY(QString candidateVersion READ candidateVersion NOTIFY firmwareUpgradeAvailableChanged)
    Q_PROPERTY(bool upgradingFirmware READ upgradingFirmware NOTIFY upgradingFirmwareChanged)
    Q_PROPERTY(QString languageVersion READ languageVersion NOTIFY languageVersionChanged)
    // base functionality
    Q_PROPERTY(ApplicationsModel* installedApps READ installedApps CONSTANT)
    Q_PROPERTY(ApplicationsModel* installedWatchfaces READ installedWatchfaces CONSTANT)
    Q_PROPERTY(QVariantMap healthParams READ healthParams WRITE setHealthParams NOTIFY healthParamsChanged)
    Q_PROPERTY(bool healthParamsReady READ healthParamsReady NOTIFY healthParamsReadyChanged)
    Q_PROPERTY(QVariantMap healthOverview READ healthOverview NOTIFY healthOverviewChanged)
    Q_PROPERTY(bool healthOverviewReady READ healthOverviewReady NOTIFY healthOverviewReadyChanged)
    Q_PROPERTY(bool healthSyncing READ healthSyncing NOTIFY healthSyncingChanged)
    Q_PROPERTY(bool imperialUnits READ imperialUnits WRITE setImperialUnits NOTIFY imperialUnitsChanged)
    // platform features
    Q_PROPERTY(ScreenshotModel* screenshots READ screenshots CONSTANT)
    Q_PROPERTY(NotificationSourceModel* notifications READ notifications CONSTANT)
    Q_PROPERTY(QVariantMap notificationsFilter READ notificationsFilter NOTIFY notificationsFilterChanged)
    Q_PROPERTY(QVariantList timelineColors READ timelineColors NOTIFY timelineColorsChanged)
    Q_PROPERTY(QVariantList timelineIcons READ timelineIcons NOTIFY timelineIconsChanged)
    Q_PROPERTY(bool timelineColorsReady READ timelineColorsReady NOTIFY timelineColorsReadyChanged)
    Q_PROPERTY(bool timelineIconsReady READ timelineIconsReady NOTIFY timelineIconsReadyChanged)
    Q_PROPERTY(QString profileWhenConnected READ profileWhenConnected WRITE setProfileWhenConnected NOTIFY profileWhenConnectedChanged)
    Q_PROPERTY(QString profileWhenDisconnected READ profileWhenDisconnected WRITE setProfileWhenDisconnected NOTIFY profileWhenDisconnectedChanged)
    Q_PROPERTY(bool calendarSyncEnabled READ calendarSyncEnabled WRITE setCalendarSyncEnabled NOTIFY calendarSyncEnabledChanged)
    Q_PROPERTY(bool settingsPageReady READ settingsPageReady NOTIFY settingsPageReadyChanged)
    // developer features
    Q_PROPERTY(bool devConnEnabled READ devConnEnabled WRITE setDevConnEnabled NOTIFY devConnEnabledChanged)
    Q_PROPERTY(bool devConnServerRunning READ devConnServerRunning NOTIFY devConnServerRunningChanged)
    Q_PROPERTY(int logLevel READ getLogLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(bool developerSettingsReady READ developerSettingsReady NOTIFY developerSettingsReadyChanged)
    // Timeline and sync
    Q_PROPERTY(bool syncAppsFromCloud READ syncAppsFromCloud WRITE setSyncAppsFromCloud NOTIFY syncAppsFromCloudChanged)
    Q_PROPERTY(bool accountAuthenticated READ accountAuthenticated NOTIFY accountAuthenticatedChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountNameChanged)
    Q_PROPERTY(QString accountEmail READ accountEmail NOTIFY accountEmailChanged)
    Q_PROPERTY(bool accountTokenPending READ accountTokenPending NOTIFY accountTokenPendingChanged)
    Q_PROPERTY(QString accountTokenError READ accountTokenError NOTIFY accountTokenErrorChanged)
    Q_PROPERTY(int timelineWindowStart READ timelineWindowStart NOTIFY timelineWindowChanged)
    Q_PROPERTY(int timelineWindowFade READ timelineWindowFade NOTIFY timelineWindowChanged)
    Q_PROPERTY(int timelineWindowEnd READ timelineWindowEnd NOTIFY timelineWindowChanged)
    Q_PROPERTY(bool timelineWindowReady READ timelineWindowReady NOTIFY timelineWindowReadyChanged)
    // Apps and features
    Q_PROPERTY(QVariantMap cannedResponses READ cannedResponses WRITE setCannedResponses NOTIFY cannedResponsesChanged)
    Q_PROPERTY(bool cannedResponsesReady READ cannedResponsesReady NOTIFY cannedResponsesReadyChanged)
    Q_PROPERTY(QVariantMap cannedContacts READ cannedContacts NOTIFY cannedContactsChanged)
    Q_PROPERTY(bool cannedContactsReady READ cannedContactsReady NOTIFY cannedContactsReadyChanged)
    Q_PROPERTY(QString weatherUnits READ weatherUnits WRITE setWeatherUnits NOTIFY weatherUnitsChanged)
    Q_PROPERTY(QString weatherLanguage READ weatherLanguage WRITE setWeatherLanguage NOTIFY weatherLanguageChanged)
    Q_PROPERTY(QString weatherApiKey READ weatherAltKey WRITE setWeatherAltKey NOTIFY weatherAltKeyChanged)
    Q_PROPERTY(QVariantList weatherLocations READ weatherLocations WRITE setWeatherLocations NOTIFY weatherLocationsChanged)
    Q_PROPERTY(bool weatherSettingsReady READ weatherSettingsReady NOTIFY weatherSettingsReadyChanged)

public:
    explicit Pebble(const QDBusObjectPath &path, QObject *parent = 0,
                    RockpoolAccount *account = 0);

    QDBusObjectPath path();

    bool connected() const;
    int connectionState() const;
    QString lastError() const;
    QString address() const;
    QString name() const;
    QString platformString() const;
    QString hardwarePlatform() const;
    QString serialNumber() const;
    QString softwareVersion() const;
    QString languageVersion() const;
    int model() const;
    bool recovery() const;
    bool upgradingFirmware() const;

    bool firmwareUpgradeAvailable() const;
    QString firmwareReleaseNotes() const;
    QString candidateVersion() const;

    QVariantMap healthParams() const;
    bool healthParamsReady() const;
    void setHealthParams(const QVariantMap &healthParams);
    QVariantMap healthOverview() const;
    bool healthOverviewReady() const;
    bool healthSyncing() const;

    bool imperialUnits() const;
    void setImperialUnits(bool imperialUnits);

    QString profileWhenConnected() const;
    void setProfileWhenConnected(const QString &profile);
    QString profileWhenDisconnected() const;
    void setProfileWhenDisconnected(const QString &profile);

    QVariantMap notificationsFilter() const;
    QVariantList timelineColors() const;
    QVariantList timelineIcons() const;
    bool timelineColorsReady() const;
    bool timelineIconsReady() const;
    bool calendarSyncEnabled() const;
    void setCalendarSyncEnabled(bool enabled);
    bool settingsPageReady() const;

    bool devConnServerRunning() const;
    bool devConnEnabled() const;
    bool developerSettingsReady() const;

    bool accountAuthenticated() const;
    QString accountName() const;
    QString accountEmail() const;
    bool accountTokenPending() const;
    QString accountTokenError() const;
    bool syncAppsFromCloud() const;

    QVariantMap cannedResponses() const;
    bool cannedResponsesReady() const;
    QVariantMap cannedContacts() const;
    bool cannedContactsReady() const;
    QVariantList weatherLocations() const;
    QString weatherLanguage() const;
    QString weatherUnits() const;
    QString weatherAltKey() const;
    bool weatherSettingsReady() const;

    ApplicationsModel* installedApps() const;
    ApplicationsModel* installedWatchfaces() const;
    NotificationSourceModel *notifications() const;
    ScreenshotModel* screenshots() const;

public slots:
    void removeApp(const QString &uuid);
    void installApp(const QString &storeId);
    void sideloadApp(const QString &packageFile);
    void requestConfigurationURL(const QString &uuid);
    void configurationClosed(const QString &uuid, const QString &url);
    void launchApp(const QString &uuid);
    void performFirmwareUpgrade();
    void loadLanguagePack(const QString &pblFile);

    void requestScreenshot();
    void removeScreenshot(const QString &filename);
    void setNotificationFilter(const QString &sourceId, int enabled);
    void forgetNotificationFilter(const QString &sourceId);
    // Per-app appearance overrides; empty value clears. colorName is a TimelineColor.name and
    // iconCode a TimelineIcon.code. The model updates optimistically while the daemon serializes
    // the combined persistence and the existing filter signal converges every open watch view.
    void setNotificationAppColor(const QString &sourceId, const QString &colorName);
    void setNotificationAppIcon(const QString &sourceId, const QString &iconCode);
    // Constant palettes for the pickers, fetched lazily from the daemon.
    void refreshTimelineColors();
    void refreshTimelineIcons();
    void refreshSettingsPage();
    void refreshHealthParams();
    void refreshHealthOverview();
    void fetchHealthData();

    void dumpLogs(const QString &filename);
    void setDevConnEnabled(bool enabled);
    void setLogLevel(int level);
    int getLogLevel() const;
    void refreshDeveloperSettings();

    void setOAuthToken(const QString &token);
    bool setOAuthTokenFromCallback(const QString &callbackUrl);
    void setSyncAppsFromCloud(bool enable);
    int timelineWindowStart() const;
    int timelineWindowFade() const;
    int timelineWindowEnd() const;
    bool timelineWindowReady() const;
    void refreshTimelineWindow();
    void setTimelineWindow(int start, int fade, int end);
    void resetTimeline();

    QVariantMap getCannedResponses(const QStringList &keys);
    void setCannedResponses(const QVariantMap &cans);
    void refreshCannedResponses();
    QVariantMap getCannedContacts(const QStringList &keys);
    void setCannedContacts(const QVariantMap &cans);
    void refreshCannedContacts();

    void setWeatherUnits(const QString &u);
    void setWeatherLanguage(const QString &l);
    void setWeatherLocations(const QVariantList &in);
    void setWeatherAltKey(const QString &key);
    void refreshWeatherSettings();

signals:
    void identityChanged();
    void connectedChanged();
    void connectionStateChanged();
    void platformStringChanged();
    void hardwarePlatformChanged();
    void modelChanged();
    void recoveryChanged();
    void softwareVersionChanged();
    void languageVersionChanged();
    void firmwareUpgradeAvailableChanged();
    void upgradingFirmwareChanged();
    void healthParamsChanged();
    void healthParamsReadyChanged();
    void healthOverviewChanged();
    void healthOverviewReadyChanged();
    void healthDataChanged();
    void healthSyncingChanged();
    void healthSyncCompleted(bool success);
    void imperialUnitsChanged();
    void openURL(const QString &uuid, const QString &url);

    void notificationsFilterChanged();
    void timelineColorsChanged();
    void timelineIconsChanged();
    void timelineColorsReadyChanged();
    void timelineIconsReadyChanged();
    void profileWhenDisconnectedChanged();
    void profileWhenConnectedChanged();
    void calendarSyncEnabledChanged();
    void settingsPageReadyChanged();

    void logsDumped(bool success);
    void devConnEnabledChanged();
    void devConnServerRunningChanged();
    void logLevelChanged();
    void developerSettingsReadyChanged();

    void accountAuthenticatedChanged();
    void accountNameChanged();
    void accountEmailChanged();
    void accountTokenPendingChanged();
    void accountTokenErrorChanged();
    void syncAppsFromCloudChanged();
    void timelineWindowChanged();
    void timelineWindowReadyChanged();

    void cannedResponsesChanged();
    void cannedResponsesReadyChanged();
    void cannedContactsChanged();
    void cannedContactsReadyChanged();
    void weatherUnitsChanged();
    void weatherLanguageChanged();
    void weatherAltKeyChanged();
    void weatherLocationsChanged();
    void weatherSettingsReadyChanged();

private:
    void requestProperty(const QString &propertyName);
    void scheduleAddressRetry(quint64 requestEpoch, quint64 serviceEpoch);
    void applyProperty(const QString &propertyName, const QVariant &value);
    void sendVoidCommand(const QString &method,
                         const QVariantList &arguments = QVariantList());
    void voidCommandReplyFinished(QDBusPendingCallWatcher *watcher);
    void refreshConnectionState();
    void requestTimelineWindowSnapshot();
    void timelineWindowPropertyReplyFinished(QDBusPendingCallWatcher *watcher);
    void timelineWindowSnapshotFailed(quint64 requestEpoch, quint64 serviceEpoch);
    void dispatchTimelineWindowWrite(int start, int fade, int end,
                                     quint64 writeEpoch);
    void timelineWindowWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void setTimelineWindowReady(bool ready);
    void refreshNotificationsAsync();
    void applyNotificationFilters(const QVariantMap &filters);
    void notificationFiltersReplyFinished(QDBusPendingCallWatcher *watcher);
    void notificationAppearanceReplyFinished(QDBusPendingCallWatcher *watcher);
    void sendNotificationFilterCommand(const QString &method,
                                       const QString &sourceId,
                                       const QVariantList &arguments);
    void notificationFilterCommandReplyFinished(QDBusPendingCallWatcher *watcher);
    void refreshTimelinePalette(const QString &method);
    void timelinePaletteReplyFinished(QDBusPendingCallWatcher *watcher);
    void setTimelinePaletteReady(const QString &method, bool ready);
    bool applySettingsProperty(const QString &propertyName, const QVariant &value);
    void markSettingsPropertyLoaded(const QString &propertyName, bool authoritative);
    void settingsPropertyReadFailed(const QString &propertyName,
                                    quint64 requestEpoch);
    void setSettingsPageReady(bool ready);
    void sendSettingsWrite(const QString &propertyName, const QString &method,
                           const QVariant &value, const QVariant &previousValue);
    void settingsWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void settingsPropertyChangedFromService(const QString &propertyName);
    void applyHealthParams(const QVariantMap &params, bool authoritative);
    void healthParamsReadFailed(quint64 requestEpoch);
    void setHealthParamsReady(bool ready);
    void healthParamsWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void healthParamsChangedFromService();
    void healthOverviewReplyFinished(QDBusPendingCallWatcher *watcher);
    void healthDataChangedFromService();
    void healthSyncReplyFinished(QDBusPendingCallWatcher *watcher);
    void setHealthOverviewReady(bool ready);
    void setHealthSyncing(bool syncing);
    void applyCannedResponses(const QVariantMap &responses, bool authoritative);
    void cannedResponsesReadFailed(quint64 requestEpoch);
    void setCannedResponsesReady(bool ready);
    void cannedResponsesWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void requestCannedContacts();
    void applyCannedContacts(const QVariantMap &contacts, bool authoritative);
    void cannedContactsReadFailed(quint64 requestEpoch);
    void setCannedContactsReady(bool ready);
    void cannedContactsReadReplyFinished(QDBusPendingCallWatcher *watcher);
    void cannedContactsWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void propertyReplyFinished(QDBusPendingCallWatcher *watcher);
    void connectionPropertyReplyFinished(QDBusPendingCallWatcher *watcher);
    void appsReplyFinished(QDBusPendingCallWatcher *watcher);
    void screenshotsReplyFinished(QDBusPendingCallWatcher *watcher);
    void firmwarePropertyReplyFinished(QDBusPendingCallWatcher *watcher);
    void requestWeatherProperty(const QString &propertyName);
    void refreshWeatherLocations();
    void applyWeatherProperty(const QString &propertyName, const QVariant &value);
    void markWeatherPropertyLoaded(const QString &propertyName);
    void setWeatherSettingsReady(bool ready);
    void sendWeatherWrite(const QString &propertyName, const QString &method,
                          const QVariant &value, const QVariant &previousValue);
    void weatherLocationsReplyFinished(QDBusPendingCallWatcher *watcher);
    void weatherWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void weatherLocationsChangedFromService(const QVariantList &locations);
    void applyDeveloperProperty(const QString &propertyName, const QVariant &value);
    void markDeveloperPropertyLoaded(const QString &propertyName);
    void setDeveloperSettingsReady(bool ready);
    void sendDeveloperWrite(const QString &propertyName, const QString &method,
                            const QVariant &value, const QVariant &previousValue,
                            const QString &refreshProperty = QString());
    void developerWriteReplyFinished(QDBusPendingCallWatcher *watcher);
    void dumpLogsReplyFinished(QDBusPendingCallWatcher *watcher);
    void logsDumpedFromService(bool success);
    void serviceOwnerChanged(const QString &service,
                             const QString &oldOwner,
                             const QString &newOwner);

private slots:
    void dataChanged();
    void refreshLanguageVersion();
    void pebbleConnected();
    void pebbleDisconnected();
    void pebbleConnectionStateChanged(int state);
    void notificationFilterChanged(const QString &sourceId, const QString &name, const QString &icon, const int enabled);
    void refreshNotifications();
    void refreshApps();
    void appsSorted();
    void refreshScreenshots();
    void screenshotAdded(const QString &filename);
    void screenshotRemoved(const QString &filename);
    void refreshFirmwareUpdateInfo();
    void devConStateChanged(bool state);

private:
    QDBusObjectPath m_path;

    bool m_connected = false;
    int m_connectionState = 0;
    QString m_lastError;
    QString m_address;
    QString m_name;
    QString m_platformString;
    QString m_hardwarePlatform;
    QString m_serialNumber;
    QString m_softwareVersion;
    QString m_languageVersion;
    bool m_recovery = false;
    int m_model = 0;
    RockworkPebbleInterface *m_iface;
    NotificationSourceModel *m_notifications;
    ApplicationsModel *m_installedApps;
    ApplicationsModel *m_installedWatchfaces;
    ScreenshotModel *m_screenshotModel;

    bool m_firmwareUpgradeAvailable = false;
    QString m_firmwareReleaseNotes;
    QString m_candidateVersion;
    bool m_upgradingFirmware = false;
    QVariantMap m_pendingFirmwareValues;
    QSet<QString> m_pendingFirmwareReplies;

    QVariantMap m_pendingConnectionValues;
    QSet<QString> m_pendingConnectionReplies;

    RockpoolAccount *m_account;
    bool m_timelineWindowReady = false;
    bool m_timelineWindowHasSnapshot = false;
    bool m_timelineWindowRequestFailed = false;
    int m_timelineWindowReadFailures = 0;
    bool m_timelineWindowWriteInFlight = false;
    bool m_timelineWindowWriteQueued = false;
    int m_queuedTimelineWindowStart = 0;
    int m_queuedTimelineWindowFade = 0;
    int m_queuedTimelineWindowEnd = 0;
    quint64 m_queuedTimelineWindowWriteEpoch = 0;
    quint64 m_inFlightTimelineWindowWriteEpoch = 0;
    quint64 m_timelineWindowRequestEpoch = 0;
    quint64 m_timelineWindowWriteEpoch = 0;
    QVariantMap m_pendingTimelineWindowValues;
    QSet<QString> m_pendingTimelineWindowReplies;
    quint64 m_notificationFiltersEpoch = 0;
    QHash<QString, quint64> m_notificationFilterCommandEpochs;
    QVariantMap m_notificationFilters;
    QVariantList m_timelineColors;
    QVariantList m_timelineIcons;
    bool m_timelineColorsRequested = false;
    bool m_timelineIconsRequested = false;
    bool m_timelineColorsReady = false;
    bool m_timelineIconsReady = false;
    bool m_timelineColorsInFlight = false;
    bool m_timelineIconsInFlight = false;
    int m_timelineColorsFailures = 0;
    int m_timelineIconsFailures = 0;
    quint64 m_timelineColorsEpoch = 0;
    quint64 m_timelineIconsEpoch = 0;

    bool m_settingsPageRequested = false;
    bool m_settingsPageReady = false;
    bool m_imperialUnits = false;
    QString m_profileWhenConnected;
    QString m_profileWhenDisconnected;
    bool m_calendarSyncEnabled = false;
    bool m_syncAppsFromCloud = false;
    QSet<QString> m_settingsLoadedProperties;
    QSet<QString> m_settingsAuthoritativeProperties;
    QHash<QString, int> m_settingsReadFailures;
    QHash<QString, quint64> m_settingsWriteEpochs;
    QHash<QString, quint64> m_settingsValueRevisions;

    bool m_healthParamsRequested = false;
    bool m_healthParamsReady = false;
    bool m_healthParamsAuthoritative = false;
    bool m_healthParamsValid = false;
    int m_healthParamsReadFailures = 0;
    quint64 m_healthParamsWriteEpoch = 0;
    quint64 m_healthParamsValueRevision = 0;
    QVariantMap m_healthParams;

    bool m_healthOverviewRequested = false;
    bool m_healthOverviewReady = false;
    bool m_healthSyncing = false;
    quint64 m_healthOverviewEpoch = 0;
    quint64 m_healthSyncEpoch = 0;
    QVariantMap m_healthOverview;

    bool m_cannedResponsesRequested = false;
    bool m_cannedResponsesReady = false;
    bool m_cannedResponsesAuthoritative = false;
    int m_cannedResponsesReadFailures = 0;
    quint64 m_cannedResponsesWriteEpoch = 0;
    quint64 m_cannedResponsesValueRevision = 0;
    QVariantMap m_cannedResponses;

    bool m_cannedContactsRequested = false;
    bool m_cannedContactsReady = false;
    bool m_cannedContactsAuthoritative = false;
    int m_cannedContactsReadFailures = 0;
    quint64 m_cannedContactsRequestEpoch = 0;
    quint64 m_cannedContactsWriteEpoch = 0;
    quint64 m_cannedContactsValueRevision = 0;
    QVariantMap m_cannedContacts;

    QDBusServiceWatcher *m_serviceWatcher;
    quint64 m_serviceEpoch = 0;
    QHash<QString, quint64> m_propertyEpochs;
    int m_addressReadFailures = 0;
    quint64 m_connectionEpoch = 0;
    quint64 m_appsEpoch = 0;
    quint64 m_screenshotsEpoch = 0;
    quint64 m_firmwareEpoch = 0;

    bool m_weatherRequested = false;
    bool m_weatherSettingsReady = false;
    QString m_weatherUnits = QStringLiteral("m");
    QString m_weatherLanguage;
    QString m_weatherAltKey;
    QVariantList m_weatherLocations;
    QSet<QString> m_weatherLoadedProperties;
    QHash<QString, quint64> m_weatherWriteEpochs;
    QHash<QString, quint64> m_weatherValueRevisions;
    quint64 m_weatherLocationsEpoch = 0;

    bool m_developerRequested = false;
    bool m_developerSettingsReady = false;
    bool m_devConnEnabled = false;
    bool m_devConnServerRunning = false;
    int m_logLevel = 1;
    QSet<QString> m_developerLoadedProperties;
    QHash<QString, quint64> m_developerWriteEpochs;
    QHash<QString, quint64> m_developerValueRevisions;

    bool m_logDumpPending = false;
    quint64 m_logDumpEpoch = 0;

    qint32 m_timelienWindowStart = 2;
    qint32 m_timelienWindowFade = 3600;
    qint32 m_timelienWindowEnd = 7;
};

#endif // PEBBLE_H
