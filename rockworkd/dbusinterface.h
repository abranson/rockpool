#ifndef DBUSINTERFACE_H
#define DBUSINTERFACE_H

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>

class Pebble;

class DBusPebble: public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockwork.Pebble")
public:
    DBusPebble(Pebble *pebble, QObject *parent);

signals:
    void Connected();
    void Disconnected();
    void NotificationFilterChanged(const QString &sourceId, const QString &name, const QString &icon, int enabled);
    void InstalledAppsChanged();
    void OpenURL(const QString &uuid, const QString &url);
    void ScreenshotAdded(const QString &filename);
    void ScreenshotRemoved(const QString &filename);
    void FirmwareUpgradeAvailableChanged();
    void UpgradingFirmwareChanged();
    void LogsDumped(bool success);

    void HealthParamsChanged();
    void ImperialUnitsChanged();
    void ProfileWhenConnectedChanged();
    void ProfileWhenDisconnectedChanged();
    void CalendarSyncEnabledChanged();

    void DevConnectionChanged(const bool state);
    void DevConnCloudChanged(const bool state);

    void oauthTokenChanged(const QString &token);

public slots:
    QString Address() const;
    QString Name() const;
    QString SerialNumber() const;
    QString HardwarePlatform() const;
    QString SoftwareVersion() const;
    int Model() const;
    bool IsConnected() const;
    bool Recovery() const;
    bool FirmwareUpgradeAvailable() const;
    QString CandidateFirmwareVersion() const;
    QString FirmwareReleaseNotes() const;
    void PerformFirmwareUpgrade();
    bool UpgradingFirmware() const;

    QString accountName() const;
    QString accountEmail() const;
    QString oauthToken() const;
    void setOAuthToken(const QString &token);

    void setTimelineWindow(qint32 start, qint32 fade, qint32 end);
    qint32 timelineWindowStart() const;
    qint32 timelineWindowFade() const;
    qint32 timelineWindowEnd() const;

    void insertTimelinePin(const QString &jsonPin);
    QVariantMap NotificationsFilter() const;
    void SetNotificationFilter(const QString &sourceId, int enabled);
    void ForgetNotificationFilter(const QString &sourceId);

    QVariantMap cannedResponses() const;
    void setCannedResponses(const QVariantMap &cans);

    bool DevConnectionEnabled() const;
    quint16 DevConnListenPort() const;
    bool DevConnectionState() const;
    bool DevConnCloudEnabled() const;
    bool DevConnCloudState() const;
    void SetDevConnEnabled(bool enabled);
    void SetDevConnCloudEnabled(bool enabled);
    void SetDevConnListenPort(quint16 port);

    void InstallApp(const QString &id);
    void SideloadApp(const QString &packageFile);
    QStringList InstalledAppIds() const;
    QVariantList InstalledApps() const;
    void RemoveApp(const QString &id);
    void ConfigurationURL(const QString &uuid);
    void ConfigurationClosed(const QString &uuid, const QString &result);
    void SetAppOrder(const QStringList &newList);
    void LaunchApp(const QString &uuid);
    void RequestScreenshot();
    QStringList Screenshots() const;
    void RemoveScreenshot(const QString &filename);
    void DumpLogs(const QString &fileName) const;

    QVariantMap HealthParams() const;
    void SetHealthParams(const QVariantMap &healthParams);

    bool ImperialUnits() const;
    void SetImperialUnits(bool imperialUnits);

    QString ProfileWhenConnected();
    void SetProfileWhenConnected(const QString &profile);

    QString ProfileWhenDisconnected();
    void SetProfileWhenDisconnected(const QString &profile);

    void onProfileConnectionSwitchChanged(bool connected);

    bool CalendarSyncEnabled() const;
    void SetCalendarSyncEnabled(bool enabled);

private:
    Pebble *m_pebble;
};

class DBusInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockwork.Manager")

public:
    explicit DBusInterface(QObject *parent = 0);

public slots:
    Q_SCRIPTABLE QString Version();
    Q_SCRIPTABLE QList<QDBusObjectPath> ListWatches();

signals:
    Q_SCRIPTABLE void PebblesChanged();
    void NameChanged();

private slots:
    void pebbleAdded(Pebble *pebble);
    void pebbleRemoved(Pebble *pebble);

private:
    QHash<QString, DBusPebble*> m_dbusPebbles;
};

#endif // DBUSINTERFACE_H
