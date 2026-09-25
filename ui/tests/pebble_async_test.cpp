/*
 * Regression coverage for asynchronous org.rockpool.Pebble bootstrap calls.
 *
 * Run with run-pebbles-async-test.sh so org.rockpool is private to this test.
 */
#include "pebble.h"
#include "applicationsmodel.h"
#include "notificationsourcemodel.h"
#include "rockpoolaccount.h"
#include "rockpooloperation.h"
#include "screenshotmodel.h"

#include <QtTest>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QMetaMethod>

namespace {
const char serviceName[] = "org.rockpool";
const char accountServiceName[] = "io.rebble.libpebble3";
const char watchPath[] = "/org/rockpool/Watch/test";
const char watchInterface[] = "org.rockpool.Pebble";
const char accountPath[] = "/io/rebble/libpebble3/Manager";
const char accountInterface[] = "io.rebble.libpebble3.Account1";
const char operationInterface[] = "io.rebble.libpebble3.Operation1";
const char propertiesInterface[] = "org.freedesktop.DBus.Properties";

static QVariantMap operationProperties(const QString &state,
                                       const QString &error = QString(),
                                       const QString &errorDetail = QString())
{
    QVariantMap properties;
    properties.insert(QStringLiteral("Kind"), QStringLiteral("account.set-token"));
    properties.insert(QStringLiteral("State"), state);
    properties.insert(QStringLiteral("Progress"),
                      state == QStringLiteral("succeeded") ? 1.0 : 0.0);
    properties.insert(QStringLiteral("Result"), QVariantMap());
    properties.insert(QStringLiteral("Error"), error);
    properties.insert(QStringLiteral("ErrorDetail"), errorDetail);
    return properties;
}

class AccountTokenOperation : public QDBusVirtualObject
{
public:
    AccountTokenOperation(const QString &path, const QDBusConnection &connection)
        : m_path(path),
          m_connection(connection),
          m_properties(operationProperties(QStringLiteral("running")))
    {
    }

    int getAllCount() const
    {
        return m_getAllCount;
    }

    void finish(bool success,
                const QString &error = QString(),
                const QString &errorDetail = QString())
    {
        m_properties = operationProperties(
            success ? QStringLiteral("succeeded") : QStringLiteral("failed"),
            error, errorDetail);

        QDBusMessage changed = QDBusMessage::createSignal(
            m_path, QString::fromLatin1(propertiesInterface),
            QStringLiteral("PropertiesChanged"));
        changed.setArguments(QVariantList()
                             << QString::fromLatin1(operationInterface)
                             << m_properties
                             << QStringList());
        QVERIFY(m_connection.send(changed));

        QDBusMessage completed = QDBusMessage::createSignal(
            m_path, QString::fromLatin1(operationInterface),
            QStringLiteral("Completed"));
        completed.setArguments(QVariantList() << success);
        QVERIFY(m_connection.send(completed));
    }

    QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return QStringLiteral("<node/>");
    }

    bool handleMessage(const QDBusMessage &message,
                       const QDBusConnection &connection) override
    {
        if (message.interface() != QString::fromLatin1(propertiesInterface) ||
                message.member() != QStringLiteral("GetAll") ||
                message.arguments().count() != 1 ||
                message.arguments().first().toString() !=
                    QString::fromLatin1(operationInterface)) {
            return false;
        }
        ++m_getAllCount;
        return connection.send(message.createReply(QVariantList() << m_properties));
    }

private:
    QString m_path;
    QDBusConnection m_connection;
    QVariantMap m_properties;
    int m_getAllCount = 0;
};

class DelayedPebble : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockpool.Pebble")

public:
    explicit DelayedPebble(const QDBusConnection &connection)
        : m_connection(connection)
    {
    }

    void defer(const QString &method)
    {
        m_deferred << method;
    }

    int pendingCount(const QString &method) const
    {
        return m_pending.value(method).count();
    }

    int receivedCount(const QString &method) const
    {
        return m_received.value(method);
    }

    void replyPending(const QString &method, int index, const QVariantList &arguments)
    {
        QVERIFY2(index >= 0 && index < m_pending.value(method).count(), qPrintable(method));
        const QDBusMessage request = m_pending[method].takeAt(index);
        QVERIFY(m_connection.send(request.createReply(arguments)));
    }

    void replyNext(const QString &method, const QVariantList &arguments)
    {
        replyPending(method, 0, arguments);
    }

    void replyPendingError(const QString &method, int index,
                           const QString &message = QStringLiteral("test failure"))
    {
        QVERIFY2(index >= 0 && index < m_pending.value(method).count(), qPrintable(method));
        const QDBusMessage request = m_pending[method].takeAt(index);
        QVERIFY(m_connection.send(request.createErrorReply(QDBusError::Failed, message)));
    }

    QVariantList arguments(const QString &method, int index = 0) const
    {
        return m_arguments.value(method).value(index);
    }

    void setAppName(const QString &name)
    {
        m_appName = name;
    }

    void setFirmwareInfo(bool available, const QString &notes, const QString &candidate)
    {
        m_firmwareAvailable = available;
        m_firmwareNotes = notes;
        m_candidateVersion = candidate;
    }

    void setWeatherLocationsValue(const QVariantList &locations)
    {
        m_weatherLocations = locations;
    }

    void setWeatherUnitsValue(const QString &units)
    {
        m_weatherUnits = units;
    }

    void setWeatherLanguageValue(const QString &language)
    {
        m_weatherLanguage = language;
    }

    void setWeatherAltKeyValue(const QString &key)
    {
        m_weatherAltKey = key;
    }

    void setDevConnectionEnabledValue(bool enabled)
    {
        m_devConnectionEnabled = enabled;
    }

    void setDevConnectionStateValue(bool running)
    {
        m_devConnectionState = running;
    }

    void setLogLevelValue(int level)
    {
        m_logLevel = level;
    }

    void setImperialUnitsValue(bool enabled)
    {
        m_imperialUnits = enabled;
    }

    void setHealthParamsValue(const QVariantMap &params)
    {
        m_healthParams = params;
    }

    void setHealthOverviewValue(const QVariantMap &overview)
    {
        m_healthOverview = overview;
    }

    void setProfileWhenConnectedValue(const QString &profile)
    {
        m_profileWhenConnected = profile;
    }

    void setProfileWhenDisconnectedValue(const QString &profile)
    {
        m_profileWhenDisconnected = profile;
    }

    void setCalendarSyncEnabledValue(bool enabled)
    {
        m_calendarSyncEnabled = enabled;
    }

    void setSyncAppsFromCloudValue(bool enabled)
    {
        m_syncAppsFromCloud = enabled;
    }

    void setTimelineWindowWireValues(int start, int fade, int end)
    {
        m_timelineWindowStart = start;
        m_timelineWindowFade = fade;
        m_timelineWindowEnd = end;
    }

    void setCannedResponsesValue(const QVariantMap &responses)
    {
        m_cannedResponses = responses;
    }

    void setFavoriteContactsValue(const QVariantMap &contacts)
    {
        m_favoriteContacts = contacts;
    }

    QVariantMap cannedResponsesWrite(int index = 0) const
    {
        return m_cannedResponsesWrites.value(index);
    }

    QVariantMap favoriteContactsWrite(int index = 0) const
    {
        return m_favoriteContactsWrites.value(index);
    }

    void emitPebbleSignal(const QString &name, const QVariantList &arguments = QVariantList())
    {
        QDBusMessage signal = QDBusMessage::createSignal(QString::fromLatin1(watchPath),
                                                          QString::fromLatin1(watchInterface),
                                                          name);
        signal.setArguments(arguments);
        QVERIFY(m_connection.send(signal));
    }

    static QVariantList appsArguments(const QString &name)
    {
        QDBusArgument app;
        app.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        app.beginMapEntry();
        app << QStringLiteral("name") << QDBusVariant(name);
        app.endMapEntry();
        app.beginMapEntry();
        app << QStringLiteral("uuid") << QDBusVariant(QStringLiteral("test-app"));
        app.endMapEntry();
        app.endMap();

        QDBusArgument apps;
        apps.beginArray(qMetaTypeId<QDBusVariant>());
        apps << QDBusVariant(QVariant::fromValue(app));
        apps.endArray();
        return QVariantList() << QVariant::fromValue(apps);
    }

    static QVariantList notificationArguments(const QString &name)
    {
        QDBusArgument notification;
        notification.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        notification.beginMapEntry();
        notification << QStringLiteral("name") << QDBusVariant(name);
        notification.endMapEntry();
        notification.beginMapEntry();
        notification << QStringLiteral("icon") << QDBusVariant(QStringLiteral("icon-lock-chat"));
        notification.endMapEntry();
        notification.beginMapEntry();
        notification << QStringLiteral("enabled") << QDBusVariant(1);
        notification.endMapEntry();
        notification.endMap();

        QDBusArgument filters;
        filters.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        filters.beginMapEntry();
        filters << QStringLiteral("test.source")
                << QDBusVariant(QVariant::fromValue(notification));
        filters.endMapEntry();
        filters.endMap();
        return QVariantList() << QVariant::fromValue(filters);
    }

    static QVariantList weatherLocationsArguments(const QVariantList &locations)
    {
        QDBusArgument locationsArgument;
        locationsArgument.beginArray(qMetaTypeId<QDBusVariant>());
        foreach (const QVariant &location, locations) {
            locationsArgument << QDBusVariant(location.toStringList());
        }
        locationsArgument.endArray();
        return QVariantList() << QVariant::fromValue(locationsArgument);
    }

    static QVariantList timelinePaletteArguments(const QVariantList &entries)
    {
        QDBusArgument palette;
        palette.beginArray(qMetaTypeId<QDBusVariant>());
        foreach (const QVariant &entry, entries) {
            QDBusArgument map;
            map.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
            foreach (const QString &key, entry.toMap().keys()) {
                map.beginMapEntry();
                map << key << QDBusVariant(entry.toMap().value(key));
                map.endMapEntry();
            }
            map.endMap();
            palette << QDBusVariant(QVariant::fromValue(map));
        }
        palette.endArray();
        return QVariantList() << QVariant::fromValue(palette);
    }

    static QVariantList cannedResponsesArguments(const QVariantMap &responses)
    {
        QDBusArgument encoded;
        encoded.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        for (QVariantMap::const_iterator it = responses.constBegin();
             it != responses.constEnd(); ++it) {
            encoded.beginMapEntry();
            encoded << it.key() << QDBusVariant(it.value().toStringList());
            encoded.endMapEntry();
        }
        encoded.endMap();
        return QVariantList() << QVariant::fromValue(encoded);
    }

    static QVariantList healthParamsArguments(const QVariantMap &params)
    {
        QDBusArgument encoded;
        encoded.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        for (QVariantMap::const_iterator it = params.constBegin();
             it != params.constEnd(); ++it) {
            encoded.beginMapEntry();
            encoded << it.key() << QDBusVariant(it.value());
            encoded.endMapEntry();
        }
        encoded.endMap();
        return QVariantList() << QVariant::fromValue(encoded);
    }

    static QVariantList healthOverviewArguments(const QVariantMap &overview)
    {
        QDBusArgument encoded;
        encoded.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        for (QVariantMap::const_iterator it = overview.constBegin();
             it != overview.constEnd(); ++it) {
            encoded.beginMapEntry();
            if (it.key() == QStringLiteral("stepsWeek")
                    || it.key() == QStringLiteral("sleepWeek")) {
                QDBusArgument week;
                week.beginArray(qMetaTypeId<QDBusVariant>());
                foreach (const QVariant &entry, it.value().toList()) {
                    QDBusArgument record;
                    record.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
                    const QVariantMap values = entry.toMap();
                    for (QVariantMap::const_iterator value = values.constBegin();
                         value != values.constEnd(); ++value) {
                        record.beginMapEntry();
                        record << value.key() << QDBusVariant(value.value());
                        record.endMapEntry();
                    }
                    record.endMap();
                    week << QDBusVariant(QVariant::fromValue(record));
                }
                week.endArray();
                encoded << it.key() << QDBusVariant(QVariant::fromValue(week));
            } else {
                encoded << it.key() << QDBusVariant(it.value());
            }
            encoded.endMapEntry();
        }
        encoded.endMap();
        return QVariantList() << QVariant::fromValue(encoded);
    }

    static QVariantList favoriteContactsArguments(const QVariantMap &contacts)
    {
        QDBusArgument encoded;
        encoded.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        for (QVariantMap::const_iterator it = contacts.constBegin();
             it != contacts.constEnd(); ++it) {
            encoded.beginMapEntry();
            encoded << it.key() << QDBusVariant(it.value().toStringList());
            encoded.endMapEntry();
        }
        encoded.endMap();
        return QVariantList() << QVariant::fromValue(encoded);
    }

public slots:
    void Name() { replyOrDelay(QStringLiteral("Name"), QVariantList() << QStringLiteral("Test Pebble")); }
    void Address() { replyOrDelay(QStringLiteral("Address"), QVariantList() << QStringLiteral("AA:BB")); }
    void SerialNumber() { replyOrDelay(QStringLiteral("SerialNumber"), QVariantList() << QStringLiteral("serial")); }
    void PlatformString() { replyOrDelay(QStringLiteral("PlatformString"), QVariantList() << QStringLiteral("aplite")); }
    void HardwarePlatform() { replyOrDelay(QStringLiteral("HardwarePlatform"), QVariantList() << QStringLiteral("aplite")); }
    void SoftwareVersion() { replyOrDelay(QStringLiteral("SoftwareVersion"), QVariantList() << QStringLiteral("v1")); }
    void LanguageVersion() { replyOrDelay(QStringLiteral("LanguageVersion"), QVariantList() << QStringLiteral("en_US")); }
    void Model() { replyOrDelay(QStringLiteral("Model"), QVariantList() << 0); }
    void Recovery() { replyOrDelay(QStringLiteral("Recovery"), QVariantList() << false); }
    void IsConnected() { replyOrDelay(QStringLiteral("IsConnected"), QVariantList() << false); }
    void ConnectionState() { replyOrDelay(QStringLiteral("ConnectionState"), QVariantList() << 0); }
    void LastError() { replyOrDelay(QStringLiteral("LastError"), QVariantList() << QString()); }
    void timelineWindowStart()
    {
        replyOrDelay(QStringLiteral("timelineWindowStart"), QVariantList() << m_timelineWindowStart);
    }
    void timelineWindowFade()
    {
        replyOrDelay(QStringLiteral("timelineWindowFade"), QVariantList() << m_timelineWindowFade);
    }
    void timelineWindowEnd()
    {
        replyOrDelay(QStringLiteral("timelineWindowEnd"), QVariantList() << m_timelineWindowEnd);
    }
    void InstalledApps() { replyOrDelay(QStringLiteral("InstalledApps"), appsArguments(m_appName)); }
    void NotificationsFilter() { replyOrDelay(QStringLiteral("NotificationsFilter"), notificationArguments(m_notificationName)); }
    void SetNotificationFilter(const QString &sourceId, int enabled)
    {
        Q_UNUSED(sourceId)
        Q_UNUSED(enabled)
        replyOrDelay(QStringLiteral("SetNotificationFilter"), QVariantList());
    }
    void ForgetNotificationFilter(const QString &sourceId)
    {
        Q_UNUSED(sourceId)
        replyOrDelay(QStringLiteral("ForgetNotificationFilter"), QVariantList());
    }
    void QuietTimeSettings()
    {
        replyOrDelay(QStringLiteral("QuietTimeSettings"), healthParamsArguments(QVariantMap()));
    }
    void SetQuietTimeSetting(const QString &key, const QString &value)
    {
        Q_UNUSED(key)
        Q_UNUSED(value)
        replyOrDelay(QStringLiteral("SetQuietTimeSetting"), QVariantList() << true);
    }
    void TimelineColors()
    {
        replyOrDelay(QStringLiteral("TimelineColors"),
                     timelinePaletteArguments(m_timelineColors));
    }
    void TimelineIcons()
    {
        replyOrDelay(QStringLiteral("TimelineIcons"),
                     timelinePaletteArguments(m_timelineIcons));
    }
    void cannedResponses()
    {
        replyOrDelay(QStringLiteral("cannedResponses"),
                     cannedResponsesArguments(m_cannedResponses));
    }
    void HealthParams()
    {
        replyOrDelay(QStringLiteral("HealthParams"), healthParamsArguments(m_healthParams));
    }
    void HealthOverview()
    {
        replyOrDelay(QStringLiteral("HealthOverview"),
                     healthOverviewArguments(m_healthOverview));
    }
    void FetchHealthData()
    {
        replyOrDelay(QStringLiteral("FetchHealthData"), QVariantList());
    }
    void SetHealthParams(const QVariantMap &params)
    {
        m_healthParamsWrites.append(params);
        replyOrDelay(QStringLiteral("SetHealthParams"), QVariantList());
    }
    void getCannedResponses(const QStringList &groups)
    {
        QVariantMap selected;
        foreach (const QString &group, groups) {
            if (m_cannedResponses.contains(group)) {
                selected.insert(group, m_cannedResponses.value(group));
            }
        }
        replyOrDelay(QStringLiteral("getCannedResponses"),
                     cannedResponsesArguments(selected));
    }
    void setCannedResponses(const QVariantMap &responses)
    {
        m_cannedResponsesWrites.append(responses);
        replyOrDelay(QStringLiteral("setCannedResponses"), QVariantList());
    }
    void getFavoriteContacts(const QStringList &names)
    {
        QVariantMap selected;
        foreach (const QString &name, names) {
            if (m_favoriteContacts.contains(name)) {
                selected.insert(name, m_favoriteContacts.value(name));
            }
        }
        if (names.isEmpty()) {
            selected = m_favoriteContacts;
        }
        replyOrDelay(QStringLiteral("getFavoriteContacts"),
                     favoriteContactsArguments(selected));
    }
    void setFavoriteContacts(const QVariantMap &contacts)
    {
        m_favoriteContactsWrites.append(contacts);
        replyOrDelay(QStringLiteral("setFavoriteContacts"), QVariantList());
    }
    void Screenshots() { replyOrDelay(QStringLiteral("Screenshots"), QVariantList() << m_screenshots); }
    void FirmwareUpgradeAvailable() { replyOrDelay(QStringLiteral("FirmwareUpgradeAvailable"), QVariantList() << m_firmwareAvailable); }
    void FirmwareReleaseNotes() { replyOrDelay(QStringLiteral("FirmwareReleaseNotes"), QVariantList() << m_firmwareNotes); }
    void CandidateFirmwareVersion() { replyOrDelay(QStringLiteral("CandidateFirmwareVersion"), QVariantList() << m_candidateVersion); }
    void UpgradingFirmware() { replyOrDelay(QStringLiteral("UpgradingFirmware"), QVariantList() << false); }
    void WeatherLocations() { replyOrDelay(QStringLiteral("WeatherLocations"), weatherLocationsArguments(m_weatherLocations)); }
    void WeatherUnits() { replyOrDelay(QStringLiteral("WeatherUnits"), QVariantList() << m_weatherUnits); }
    void WeatherLanguage() { replyOrDelay(QStringLiteral("WeatherLanguage"), QVariantList() << m_weatherLanguage); }
    void WeatherAltKey() { replyOrDelay(QStringLiteral("WeatherAltKey"), QVariantList() << m_weatherAltKey); }
    void SetWeatherLocations(const QVariantList &locations)
    {
        m_weatherLocations = locations;
        replyOrDelay(QStringLiteral("SetWeatherLocations"), QVariantList());
    }
    void setWeatherUnits(const QString &units)
    {
        m_weatherUnits = units;
        replyOrDelay(QStringLiteral("setWeatherUnits"), QVariantList());
    }
    void setWeatherLanguage(const QString &language)
    {
        m_weatherLanguage = language;
        replyOrDelay(QStringLiteral("setWeatherLanguage"), QVariantList());
    }
    void setWeatherAltKey(const QString &key)
    {
        m_weatherAltKey = key;
        replyOrDelay(QStringLiteral("setWeatherAltKey"), QVariantList());
    }
    void ImperialUnits()
    {
        replyOrDelay(QStringLiteral("ImperialUnits"), QVariantList() << m_imperialUnits);
    }
    void ProfileWhenConnected()
    {
        replyOrDelay(QStringLiteral("ProfileWhenConnected"),
                     QVariantList() << m_profileWhenConnected);
    }
    void ProfileWhenDisconnected()
    {
        replyOrDelay(QStringLiteral("ProfileWhenDisconnected"),
                     QVariantList() << m_profileWhenDisconnected);
    }
    void CalendarSyncEnabled()
    {
        replyOrDelay(QStringLiteral("CalendarSyncEnabled"),
                     QVariantList() << m_calendarSyncEnabled);
    }
    void syncAppsFromCloud()
    {
        replyOrDelay(QStringLiteral("syncAppsFromCloud"),
                     QVariantList() << m_syncAppsFromCloud);
    }
    void SetImperialUnits(bool enabled)
    {
        m_imperialUnits = enabled;
        replyOrDelay(QStringLiteral("SetImperialUnits"), QVariantList());
    }
    void SetProfileWhenConnected(const QString &profile)
    {
        m_profileWhenConnected = profile;
        replyOrDelay(QStringLiteral("SetProfileWhenConnected"), QVariantList());
    }
    void SetProfileWhenDisconnected(const QString &profile)
    {
        m_profileWhenDisconnected = profile;
        replyOrDelay(QStringLiteral("SetProfileWhenDisconnected"), QVariantList());
    }
    void SetCalendarSyncEnabled(bool enabled)
    {
        m_calendarSyncEnabled = enabled;
        replyOrDelay(QStringLiteral("SetCalendarSyncEnabled"), QVariantList());
    }
    void setSyncAppsFromCloud(bool enabled)
    {
        m_syncAppsFromCloud = enabled;
        replyOrDelay(QStringLiteral("setSyncAppsFromCloud"), QVariantList());
    }
    void DevConnectionEnabled()
    {
        replyOrDelay(QStringLiteral("DevConnectionEnabled"),
                     QVariantList() << m_devConnectionEnabled);
    }
    void DevConnectionState()
    {
        replyOrDelay(QStringLiteral("DevConnectionState"),
                     QVariantList() << m_devConnectionState);
    }
    void getLogLevel()
    {
        replyOrDelay(QStringLiteral("getLogLevel"), QVariantList() << m_logLevel);
    }
    void SetDevConnEnabled(bool enabled)
    {
        m_devConnectionEnabled = enabled;
        replyOrDelay(QStringLiteral("SetDevConnEnabled"), QVariantList());
    }
    void setLogLevel(int level)
    {
        m_logLevel = level;
        replyOrDelay(QStringLiteral("setLogLevel"), QVariantList());
    }
    void DumpLogs(const QString &filename)
    {
        Q_UNUSED(filename)
        replyOrDelay(QStringLiteral("DumpLogs"), QVariantList());
    }
    void LoadLanguagePack(const QString &filename)
    {
        Q_UNUSED(filename)
        replyOrDelay(QStringLiteral("LoadLanguagePack"), QVariantList());
    }
    void ConfigurationClosed(const QString &uuid, const QString &url)
    {
        Q_UNUSED(uuid)
        Q_UNUSED(url)
        replyOrDelay(QStringLiteral("ConfigurationClosed"), QVariantList());
    }
    void LaunchApp(const QString &uuid)
    {
        Q_UNUSED(uuid)
        replyOrDelay(QStringLiteral("LaunchApp"), QVariantList());
    }
    void ConfigurationURL(const QString &uuid)
    {
        Q_UNUSED(uuid)
        replyOrDelay(QStringLiteral("ConfigurationURL"), QVariantList());
    }
    void RemoveApp(const QString &uuid)
    {
        Q_UNUSED(uuid)
        replyOrDelay(QStringLiteral("RemoveApp"), QVariantList());
    }
    void InstallApp(const QString &storeId)
    {
        Q_UNUSED(storeId)
        replyOrDelay(QStringLiteral("InstallApp"), QVariantList());
    }
    void SideloadApp(const QString &packageFile)
    {
        Q_UNUSED(packageFile)
        replyOrDelay(QStringLiteral("SideloadApp"), QVariantList());
    }
    void SetAppOrder(const QStringList &order)
    {
        Q_UNUSED(order)
        replyOrDelay(QStringLiteral("SetAppOrder"), QVariantList());
    }
    void RequestScreenshot()
    {
        replyOrDelay(QStringLiteral("RequestScreenshot"), QVariantList());
    }
    void RemoveScreenshot(const QString &filename)
    {
        Q_UNUSED(filename)
        replyOrDelay(QStringLiteral("RemoveScreenshot"), QVariantList());
    }
    void PerformFirmwareUpgrade()
    {
        replyOrDelay(QStringLiteral("PerformFirmwareUpgrade"), QVariantList());
    }
    void setOAuthToken(const QString &token)
    {
        Q_UNUSED(token)
        replyOrDelay(QStringLiteral("setOAuthToken"), QVariantList());
    }
    void resetTimeline()
    {
        replyOrDelay(QStringLiteral("resetTimeline"), QVariantList());
    }
    void setTimelineWindow(int start, int fade, int end)
    {
        Q_UNUSED(start)
        Q_UNUSED(fade)
        Q_UNUSED(end)
        replyOrDelay(QStringLiteral("setTimelineWindow"), QVariantList());
    }

private:
    void replyOrDelay(const QString &method, const QVariantList &arguments)
    {
        setDelayedReply(true);
        ++m_received[method];
        m_arguments[method].append(message().arguments());
        if (m_deferred.contains(method)) {
            m_pending[method].append(message());
            return;
        }
        QVERIFY(m_connection.send(message().createReply(arguments)));
    }

    QDBusConnection m_connection;
    QStringList m_deferred;
    QHash<QString, QList<QDBusMessage> > m_pending;
    QHash<QString, int> m_received;
    QHash<QString, QList<QVariantList> > m_arguments;
    QString m_appName = QStringLiteral("baseline-app");
    QString m_notificationName = QStringLiteral("baseline-source");
    QStringList m_screenshots;
    bool m_firmwareAvailable = false;
    QString m_firmwareNotes;
    QString m_candidateVersion;
    QVariantList m_weatherLocations;
    QString m_weatherUnits = QStringLiteral("m");
    QString m_weatherLanguage;
    QString m_weatherAltKey;
    bool m_devConnectionEnabled = false;
    bool m_devConnectionState = false;
    int m_logLevel = 1;
    bool m_imperialUnits = false;
    QVariantMap m_healthParams;
    QVariantMap m_healthOverview;
    QList<QVariantMap> m_healthParamsWrites;
    QString m_profileWhenConnected;
    QString m_profileWhenDisconnected;
    bool m_calendarSyncEnabled = false;
    bool m_syncAppsFromCloud = false;
    int m_timelineWindowStart = -2;
    int m_timelineWindowFade = -3600;
    int m_timelineWindowEnd = 7;
    QVariantMap m_cannedResponses;
    QList<QVariantMap> m_cannedResponsesWrites;
    QVariantMap m_favoriteContacts;
    QList<QVariantMap> m_favoriteContactsWrites;
    QVariantList m_timelineColors;
    QVariantList m_timelineIcons;
};

class DelayedAccountProperties : public QDBusVirtualObject
{
public:
    explicit DelayedAccountProperties(const QVariantMap &properties = QVariantMap())
        : m_properties(properties)
    {
    }

    void deferGetAll()
    {
        m_deferred = true;
    }

    void deferSetOAuthToken()
    {
        m_tokenDeferred = true;
    }

    void setProperties(const QVariantMap &properties)
    {
        m_properties = properties;
    }

    int receivedCount() const
    {
        return m_received;
    }

    int pendingCount() const
    {
        return m_pending.count();
    }

    void replyNext(const QDBusConnection &connection)
    {
        QVERIFY(!m_pending.isEmpty());
        const QDBusMessage request = m_pending.takeFirst();
        QVERIFY(connection.send(request.createReply(QVariantList() << m_properties)));
    }

    int tokenReceivedCount() const
    {
        return m_tokenArguments.count();
    }

    int tokenPendingCount() const
    {
        return m_pendingTokens.count();
    }

    QString tokenArgument(int index) const
    {
        return m_tokenArguments.value(index);
    }

    void replyToken(const QDBusConnection &connection, int index,
                    const QString &operationPath)
    {
        QVERIFY(index >= 0 && index < m_pendingTokens.count());
        const QDBusMessage request = m_pendingTokens.takeAt(index);
        QVERIFY(connection.send(request.createReply(
            QVariantList() << QVariant::fromValue(QDBusObjectPath(operationPath)))));
    }

    void replyNextToken(const QDBusConnection &connection,
                        const QString &operationPath)
    {
        replyToken(connection, 0, operationPath);
    }

    void replyTokenError(const QDBusConnection &connection, int index,
                         const QString &detail)
    {
        QVERIFY(index >= 0 && index < m_pendingTokens.count());
        const QDBusMessage request = m_pendingTokens.takeAt(index);
        QVERIFY(connection.send(request.createErrorReply(QDBusError::Failed, detail)));
    }

    QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return QStringLiteral("<node/>");
    }

    bool handleMessage(const QDBusMessage &message,
                       const QDBusConnection &connection) override
    {
        if (message.interface() == QString::fromLatin1(accountInterface) &&
                message.member() == QStringLiteral("SetOAuthToken") &&
                message.arguments().count() == 1) {
            m_tokenArguments.append(message.arguments().first().toString());
            if (m_tokenDeferred) {
                m_pendingTokens.append(message);
                return true;
            }
            return connection.send(message.createErrorReply(
                QDBusError::Failed, QStringLiteral("unexpected SetOAuthToken call")));
        }
        if (message.interface() != QString::fromLatin1(propertiesInterface) ||
                message.member() != QStringLiteral("GetAll")) {
            return false;
        }
        ++m_received;
        if (m_deferred) {
            m_pending.append(message);
        } else {
            if (!connection.send(message.createReply(QVariantList() << m_properties))) {
                return false;
            }
        }
        return true;
    }

private:
    QVariantMap m_properties;
    bool m_deferred = false;
    bool m_tokenDeferred = false;
    int m_received = 0;
    QList<QDBusMessage> m_pending;
    QStringList m_tokenArguments;
    QList<QDBusMessage> m_pendingTokens;
};

class PebbleAsyncTest : public QObject
{
    Q_OBJECT

private slots:
    void quietTimeAsyncSaveFailureAndSignals();
    void quietTimeIgnoresOldOwnerReply();
    void notificationIconsResolveInstalledApplications();
    void constructorQueuesHeldBootstrapCalls();
    void addressBootstrapRetriesAfterInvalidReplies();
    void newestAppsReplyWins();
    void newestNotificationReplyWins();
    void notificationFilterCommandsDoNotWaitForReplies();
    void staleNotificationFilterCommandErrorIsIgnored();
    void oldOwnerNotificationFilterCommandErrorIsIgnored();
    void newestScreenshotReplyWins();
    void newestFirmwareReplyWins();
    void oldOwnerAppReplyIsIgnored();
    void timelinePalettesLoadLazilyAndIndependently();
    void failedTimelinePaletteFetchesCanRetry();
    void oldOwnerTimelinePaletteRepliesAreIgnored();
    void settingsPageLoadsLazilyAndAtomically();
    void newestSettingsPageReplyWins();
    void settingsPageSignalsRefreshNewestValues();
    void oldOwnerSettingsPageReplyIsIgnored();
    void settingsPageSettersDoNotWaitAndRemainIndependent();
    void failedSettingsPageSettersRollBackAndReadBack();
    void staleSettingsPageSetterRepliesAreIgnored();
    void settingsSignalMakesCachedValueWritable();
    void failedSettingsReadFallsBackAndMakesValueWritable();
    void healthParamsLoadLazilyAndDecodeDbusVariantMap();
    void newestHealthParamsReplyWinsAfterSignal();
    void healthParamsWriteDoesNotWaitAndReadsBack();
    void staleHealthParamsWriteErrorIsIgnored();
    void oldOwnerHealthParamsReplyAndErrorAreIgnored();
    void coldHealthParamsReadFailureRemainsNotReady();
    void retainedHealthParamsFallbackRemainsWritable();
    void healthParamsAndImperialUnitsRemainIndependent();
    void healthOverviewLoadsLazilyAndDecodesNestedDbusValues();
    void healthDataChangedRefreshesNewestOverview();
    void oldOwnerHealthOverviewReplyIsIgnored();
    void fetchHealthDataDoesNotWaitAndRefreshesOverview();
    void fetchHealthDataErrorCompletesOnce();
    void failedHealthOverviewRetainsValidatedSnapshot();
    void healthHistoryValidatesDatesAndPresence();
    void cannedResponsesLoadLazilyAndUseCachedGetter();
    void newestCannedResponsesReplyWins();
    void oldOwnerCannedResponsesReplyIsIgnored();
    void cannedResponsesWritesMergeClearAndReadBack();
    void failedCannedResponsesWriteRollsBackAndReadsBack();
    void staleCannedResponsesWriteErrorIsIgnored();
    void failedCannedResponsesReadFallsBackAndRemainsWritable();
    void cannedContactsLoadLazilyAndUseCachedGetter();
    void newestCannedContactsReplyWins();
    void oldOwnerCannedContactsReplyIsIgnored();
    void cannedContactsWritesReplaceClearAndReadBack();
    void failedCannedContactsWriteRollsBackAndReadsBack();
    void staleCannedContactsWriteErrorIsIgnored();
    void failedCannedContactsReadFallsBackAndRemainsWritable();
    void weatherSettingsLoadLazilyAndAtomically();
    void newestWeatherSettingsReplyWins();
    void oldOwnerWeatherSettingsReplyIsIgnored();
    void weatherSettersDoNotWaitForReplies();
    void failedWeatherSetterRefreshesAndRollsBack();
    void developerSettingsLoadLazilyAndAtomically();
    void newestDeveloperSettingsReplyWins();
    void developerConnectionSignalRefreshesCurrentValues();
    void oldOwnerDeveloperSettingsReplyIsIgnored();
    void developerSettersDoNotWaitForReplies();
    void failedDeveloperSettersRefreshAndRollBack();
    void dumpLogsDoesNotWaitForReplyAndReportsError();
    void dumpLogsOwnerChangeFailsOnceAndIgnoresOldCompletion();
    void timelineActionsDoNotWaitAndWindowReadbackIsCanonical();
    void resetTimelineErrorDoesNotChangeTimelineWindow();
    void rejectedTimelineWindowWriteRollsBackToCanonicalReadback();
    void newerTimelineWindowWriteBeatsOlderReplyAndReadback();
    void timelineWindowAcceptsNegativeEnd();
    void thirdQueuedTimelineWriteReplacesSecond();
    void refreshTimelineWindowWaitsForInFlightWrite();
    void ownerReplacementRejectsOldTimelineReadback();
    void ownerReplacementCannotRestoreOldTimelineSnapshotAfterReadFailures();
    void timelineWindowReadFailuresAreBoundedAndRetainValidatedSnapshot();
    void oldOwnerTimelineActionRepliesAreIgnored();
    void compatibilityCommandsDoNotWaitAndPreserveArguments();
    void compatibilityCommandErrorsDoNotSynthesizeState();
    void oldOwnerCompatibilityCommandReplyIsIgnored();
    void oauthTokenCallbackParsing_data();
    void oauthTokenCallbackParsing();
    void oauthUsesPrimaryOperationAndRefreshesOnlyAtTerminal();
    void oauthOperationFailureIsExposed();
    void olderOAuthOperationIsRetainedButCannotFinishLatestWrite();
    void staleOAuthMethodReplyIsIgnored();
    void accountOwnerReplacementRejectsOldOAuthWork();
    void oauthTransportErrorIsExposed();
    void pebbleFacadesShareLatestOAuthOperationState();
    void sharedOAuthOperationSurvivesPebbleReplacement();
    void accountOwnerAcquisitionRefreshesCachedValues();
    void oldAccountOwnerReplyIsIgnored();
    void accountOwnerLossClearsCachedValues();
};

static void registerWatch(DelayedPebble *watch, QDBusConnection &connection)
{
    QVERIFY(connection.registerObject(QString::fromLatin1(watchPath), watch,
                                      QDBusConnection::ExportAllSlots));
}

static void registerService(const QDBusConnection &connection,
                            QDBusConnectionInterface::ServiceQueueOptions options =
                                QDBusConnectionInterface::DontQueueService,
                            QDBusConnectionInterface::ServiceReplacementOptions replacement =
                                QDBusConnectionInterface::DontAllowReplacement)
{
    QVERIFY(connection.interface()->registerService(QString::fromLatin1(serviceName),
                                                    options, replacement).isValid());
}

static void registerAccountService(DelayedAccountProperties *properties,
                                   QDBusConnection &connection,
                                   QDBusConnectionInterface::ServiceQueueOptions options =
                                       QDBusConnectionInterface::DontQueueService,
                                   QDBusConnectionInterface::ServiceReplacementOptions replacement =
                                       QDBusConnectionInterface::DontAllowReplacement)
{
    QVERIFY(connection.registerVirtualObject(QString::fromLatin1(accountPath), properties));
    QVERIFY(connection.interface()->registerService(QString::fromLatin1(accountServiceName),
                                                    options, replacement).isValid());
}

static void registerAccountOperation(AccountTokenOperation *operation,
                                     const QString &path,
                                     QDBusConnection &connection)
{
    QVERIFY(connection.registerVirtualObject(path, operation));
}

static QVariantMap accountProperties(bool authenticated = false,
                                     const QString &name = QString(),
                                     const QString &email = QString())
{
    QVariantMap properties;
    properties.insert(QStringLiteral("Authenticated"), authenticated);
    properties.insert(QStringLiteral("Name"), name);
    properties.insert(QStringLiteral("Email"), email);
    return properties;
}

static QVariantList weatherLocations(const QString &name,
                                     const QString &latitude,
                                     const QString &longitude)
{
    return QVariantList() << (QStringList() << name << latitude << longitude);
}

static QVariantList timelinePalette(const QString &key, const QString &value)
{
    QVariantMap entry;
    if (key == QStringLiteral("name")) {
        entry.insert(QStringLiteral("name"), value);
        entry.insert(QStringLiteral("rgb"), QStringLiteral("#123456"));
    } else {
        entry.insert(QStringLiteral("name"), QStringLiteral("Test icon"));
        entry.insert(QStringLiteral("code"), value);
    }
    return QVariantList() << entry;
}

static QVariantList malformedTimelinePalette(const QString &key, const QString &value)
{
    QVariantMap entry;
    entry.insert(key, value);
    return QVariantList() << entry;
}

static QVariantMap cannedResponseMap(const QString &firstSource,
                                     const QStringList &firstResponses,
                                     const QString &secondSource = QString(),
                                     const QStringList &secondResponses = QStringList())
{
    QVariantMap responses;
    responses.insert(firstSource, firstResponses);
    if (!secondSource.isEmpty()) {
        responses.insert(secondSource, secondResponses);
    }
    return responses;
}

static QVariantMap healthParams(bool enabled, int age, const QString &gender,
                                int height, int weight, bool moreActive, bool sleepMore)
{
    QVariantMap params;
    params.insert(QStringLiteral("enabled"), enabled);
    params.insert(QStringLiteral("age"), age);
    params.insert(QStringLiteral("gender"), gender);
    params.insert(QStringLiteral("height"), height);
    params.insert(QStringLiteral("weight"), weight);
    params.insert(QStringLiteral("moreActive"), moreActive);
    params.insert(QStringLiteral("sleepMore"), sleepMore);
    return params;
}

static QVariantMap decodedHealthParams(const QVariant &encoded)
{
    QVariantMap map = encoded.toMap();
    if (encoded.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = encoded.value<QDBusArgument>();
        argument >> map;
    }
    QVariantMap decoded;
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        QVariant value = it.value();
        if (value.userType() == qMetaTypeId<QDBusVariant>()) {
            value = value.value<QDBusVariant>().variant();
        }
        decoded.insert(it.key(), value);
    }
    return decoded;
}

static QVariantMap healthOverview(int todaySteps, const QString &label)
{
    QVariantList stepsWeek;
    QVariantList sleepWeek;
    for (int day = 0; day < 7; ++day) {
        QVariantMap steps;
        steps.insert(QStringLiteral("label"), label + QString::number(day));
        steps.insert(QStringLiteral("date"), QStringLiteral("2026-08-%1").arg(day + 1, 2, 10,
                                                                        QLatin1Char('0')));
        steps.insert(QStringLiteral("steps"), todaySteps + day);
        steps.insert(QStringLiteral("averageHeartRate"), 60 + day);
        steps.insert(QStringLiteral("maxHeartRate"), 100 + day);
        stepsWeek.append(steps);

        QVariantMap sleep;
        sleep.insert(QStringLiteral("label"), label + QString::number(day));
        sleep.insert(QStringLiteral("date"), QStringLiteral("2026-08-%1").arg(day + 1, 2, 10,
                                                                        QLatin1Char('0')));
        sleep.insert(QStringLiteral("sleepDuration"), 25000 + day);
        sleep.insert(QStringLiteral("deepSleepDuration"), 5000 + day);
        sleepWeek.append(sleep);
    }

    QVariantMap overview;
    overview.insert(QStringLiteral("todaySteps"), todaySteps);
    overview.insert(QStringLiteral("averageStepsPerDay"), 7000);
    overview.insert(QStringLiteral("lastNightSleepSeconds"), 27000);
    overview.insert(QStringLiteral("lastNightDeepSleepSeconds"), 5400);
    overview.insert(QStringLiteral("averageSleepSecondsPerDay"), 26000);
    overview.insert(QStringLiteral("todayAverageHeartRate"), 65);
    overview.insert(QStringLiteral("todayMaxHeartRate"), 120);
    overview.insert(QStringLiteral("latestHeartRate"), 72);
    overview.insert(QStringLiteral("latestHeartRateTimestamp"), 1785600000);
    overview.insert(QStringLiteral("averageHeartRate30Days"), 68);
    overview.insert(QStringLiteral("latestDataTimestamp"), 1785600100);
    overview.insert(QStringLiteral("daysOfData"), 9);
    overview.insert(QStringLiteral("stepsWeek"), stepsWeek);
    overview.insert(QStringLiteral("sleepWeek"), sleepWeek);
    return overview;
}

static QVariantMap decodedCannedResponseMap(const QVariantMap &encoded)
{
    QVariantMap decoded;
    for (QVariantMap::const_iterator it = encoded.constBegin();
         it != encoded.constEnd(); ++it) {
        QVariant value = it.value();
        if (value.userType() == qMetaTypeId<QDBusVariant>()) {
            value = value.value<QDBusVariant>().variant();
        }

        QStringList strings;
        if (value.userType() == qMetaTypeId<QDBusArgument>()) {
            const QDBusArgument argument = value.value<QDBusArgument>();
            argument.beginArray();
            while (!argument.atEnd()) {
                QString string;
                argument >> string;
                strings.append(string);
            }
            argument.endArray();
        } else {
            strings = value.toStringList();
        }
        decoded.insert(it.key(), strings);
    }
    return decoded;
}

static void deferCoreSettingsPage(DelayedPebble *watch)
{
    watch->defer(QStringLiteral("ImperialUnits"));
    watch->defer(QStringLiteral("ProfileWhenConnected"));
    watch->defer(QStringLiteral("ProfileWhenDisconnected"));
    watch->defer(QStringLiteral("CalendarSyncEnabled"));
}

static void deferSettingsPage(DelayedPebble *watch)
{
    deferCoreSettingsPage(watch);
    watch->defer(QStringLiteral("syncAppsFromCloud"));
}

static void replyCoreSettingsPage(DelayedPebble *watch, bool imperialUnits,
                                  const QString &connectedProfile,
                                  const QString &disconnectedProfile,
                                  bool calendarSyncEnabled)
{
    watch->replyNext(QStringLiteral("ImperialUnits"), QVariantList() << imperialUnits);
    watch->replyNext(QStringLiteral("ProfileWhenConnected"),
                     QVariantList() << connectedProfile);
    watch->replyNext(QStringLiteral("ProfileWhenDisconnected"),
                     QVariantList() << disconnectedProfile);
    watch->replyNext(QStringLiteral("CalendarSyncEnabled"),
                     QVariantList() << calendarSyncEnabled);
}

static void replySettingsPage(DelayedPebble *watch, bool imperialUnits,
                              const QString &connectedProfile,
                              const QString &disconnectedProfile,
                              bool calendarSyncEnabled,
                              bool syncAppsFromCloud = false)
{
    replyCoreSettingsPage(watch, imperialUnits, connectedProfile,
                          disconnectedProfile, calendarSyncEnabled);
    watch->replyNext(QStringLiteral("syncAppsFromCloud"),
                     QVariantList() << syncAppsFromCloud);
}

static void deferWeatherSettings(DelayedPebble *watch)
{
    watch->defer(QStringLiteral("WeatherLocations"));
    watch->defer(QStringLiteral("WeatherUnits"));
    watch->defer(QStringLiteral("WeatherLanguage"));
    watch->defer(QStringLiteral("WeatherAltKey"));
}

static void replyWeatherSettings(DelayedPebble *watch, const QVariantList &locations,
                                 const QString &units, const QString &language,
                                 const QString &key)
{
    watch->replyNext(QStringLiteral("WeatherLocations"),
                     DelayedPebble::weatherLocationsArguments(locations));
    watch->replyNext(QStringLiteral("WeatherUnits"), QVariantList() << units);
    watch->replyNext(QStringLiteral("WeatherLanguage"), QVariantList() << language);
    watch->replyNext(QStringLiteral("WeatherAltKey"), QVariantList() << key);
}

static void deferDeveloperSettings(DelayedPebble *watch)
{
    watch->defer(QStringLiteral("DevConnectionEnabled"));
    watch->defer(QStringLiteral("DevConnectionState"));
    watch->defer(QStringLiteral("getLogLevel"));
}

static void replyDeveloperSettings(DelayedPebble *watch, bool enabled,
                                   bool running, int logLevel)
{
    watch->replyNext(QStringLiteral("DevConnectionEnabled"), QVariantList() << enabled);
    watch->replyNext(QStringLiteral("DevConnectionState"), QVariantList() << running);
    watch->replyNext(QStringLiteral("getLogLevel"), QVariantList() << logLevel);
}

void PebbleAsyncTest::constructorQueuesHeldBootstrapCalls()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-constructor"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    watch.defer(QStringLiteral("InstalledApps"));
    watch.defer(QStringLiteral("NotificationsFilter"));
    watch.defer(QStringLiteral("Screenshots"));
    watch.defer(QStringLiteral("FirmwareUpgradeAvailable"));

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 elapsed = timer.elapsed();

    QTRY_COMPARE(watch.pendingCount(QStringLiteral("InstalledApps")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("NotificationsFilter")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Screenshots")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("FirmwareUpgradeAvailable")), 1);
    watch.replyNext(QStringLiteral("InstalledApps"), DelayedPebble::appsArguments(QStringLiteral("released-app")));
    watch.replyNext(QStringLiteral("NotificationsFilter"), DelayedPebble::notificationArguments(QStringLiteral("released-source")));
    watch.replyNext(QStringLiteral("Screenshots"), QVariantList() << QStringList());
    watch.replyNext(QStringLiteral("FirmwareUpgradeAvailable"), QVariantList() << false);
    const bool unregistered = connection.interface()->unregisterService(
                QString::fromLatin1(serviceName)).isValid();
    qDebug() << "held-bootstrap constructor elapsed" << elapsed;
    QVERIFY2(elapsed < 1000, "Pebble construction blocked on bootstrap reply");
    QVERIFY(unregistered);
}

void PebbleAsyncTest::addressBootstrapRetriesAfterInvalidReplies()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-address-retry"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    watch.defer(QStringLiteral("Address"));

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Address")), 1);

    watch.replyPendingError(QStringLiteral("Address"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Address")), 1);
    QCOMPARE(pebble.address(), QString());

    watch.replyNext(QStringLiteral("Address"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Address")), 1);
    QCOMPARE(pebble.address(), QString());

    watch.replyNext(QStringLiteral("Address"), QVariantList() << 1234);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Address")), 1);
    QCOMPARE(pebble.address(), QString());

    watch.replyNext(QStringLiteral("Address"), QVariantList() << QString());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Address")), 1);
    QCOMPARE(pebble.address(), QString());

    watch.replyNext(QStringLiteral("Address"),
                    QVariantList() << QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QTRY_COMPARE(pebble.address(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestAppsReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-apps"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("InstalledApps")), 1);

    watch.defer(QStringLiteral("InstalledApps"));
    watch.emitPebbleSignal(QStringLiteral("InstalledAppsChanged"));
    watch.emitPebbleSignal(QStringLiteral("InstalledAppsChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("InstalledApps")), 2);
    watch.replyPending(QStringLiteral("InstalledApps"), 1,
                       DelayedPebble::appsArguments(QStringLiteral("newest-app")));
    QTRY_COMPARE(pebble.installedApps()->rowCount(), 1);
    QTRY_COMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("newest-app"));
    watch.replyNext(QStringLiteral("InstalledApps"), DelayedPebble::appsArguments(QStringLiteral("oldest-app")));
    QTest::qWait(50);
    QCOMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("newest-app"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestNotificationReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-notifications"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("NotificationsFilter")), 1);

    watch.defer(QStringLiteral("NotificationsFilter"));
    const QVariantList changed = QVariantList() << QStringLiteral("test.source")
                                               << QStringLiteral("signal-source")
                                               << QStringLiteral("icon-lock-chat") << 1;
    watch.emitPebbleSignal(QStringLiteral("NotificationFilterChanged"), changed);
    watch.emitPebbleSignal(QStringLiteral("NotificationFilterChanged"), changed);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("NotificationsFilter")), 2);
    watch.replyPending(QStringLiteral("NotificationsFilter"), 1,
                       DelayedPebble::notificationArguments(QStringLiteral("newest-source")));
    QTRY_COMPARE(pebble.notifications()->rowCount(), 1);
    QTRY_COMPARE(pebble.notifications()->data(pebble.notifications()->index(0),
                                               NotificationSourceModel::RoleName).toString(),
                 QStringLiteral("newest-source"));
    watch.replyNext(QStringLiteral("NotificationsFilter"),
                    DelayedPebble::notificationArguments(QStringLiteral("oldest-source")));
    QTest::qWait(50);
    QCOMPARE(pebble.notifications()->data(pebble.notifications()->index(0),
                                          NotificationSourceModel::RoleName).toString(),
             QStringLiteral("newest-source"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestScreenshotReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-screenshots"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("Screenshots")), 1);

    watch.defer(QStringLiteral("Screenshots"));
    watch.emitPebbleSignal(QStringLiteral("Connected"));
    watch.emitPebbleSignal(QStringLiteral("Connected"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("Screenshots")), 2);
    watch.replyPending(QStringLiteral("Screenshots"), 1,
                       QVariantList() << (QStringList() << QStringLiteral("newest.png")));
    QTRY_COMPARE(pebble.screenshots()->latestScreenshot(), QStringLiteral("newest.png"));
    watch.replyNext(QStringLiteral("Screenshots"),
                    QVariantList() << (QStringList() << QStringLiteral("oldest.png")));
    QTest::qWait(50);
    QCOMPARE(pebble.screenshots()->latestScreenshot(), QStringLiteral("newest.png"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestFirmwareReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-firmware"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("FirmwareUpgradeAvailable")), 1);

    watch.defer(QStringLiteral("FirmwareUpgradeAvailable"));
    watch.setFirmwareInfo(true, QStringLiteral("new release notes"), QStringLiteral("v2"));
    watch.emitPebbleSignal(QStringLiteral("FirmwareUpgradeAvailableChanged"));
    watch.emitPebbleSignal(QStringLiteral("FirmwareUpgradeAvailableChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("FirmwareUpgradeAvailable")), 2);
    watch.replyPending(QStringLiteral("FirmwareUpgradeAvailable"), 1, QVariantList() << true);
    QTRY_VERIFY(pebble.firmwareUpgradeAvailable());
    QTRY_COMPARE(pebble.firmwareReleaseNotes(), QStringLiteral("new release notes"));
    QTRY_COMPARE(pebble.candidateVersion(), QStringLiteral("v2"));
    watch.replyNext(QStringLiteral("FirmwareUpgradeAvailable"), QVariantList() << false);
    QTest::qWait(50);
    QVERIFY(pebble.firmwareUpgradeAvailable());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerAppReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(firstWatch.receivedCount(QStringLiteral("InstalledApps")), 1);

    firstWatch.defer(QStringLiteral("InstalledApps"));
    firstWatch.emitPebbleSignal(QStringLiteral("InstalledAppsChanged"));
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("InstalledApps")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.setAppName(QStringLiteral("new-owner-app"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    secondWatch.emitPebbleSignal(QStringLiteral("InstalledAppsChanged"));
    QTRY_COMPARE(pebble.installedApps()->rowCount(), 1);
    QTRY_COMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("new-owner-app"));
    firstWatch.replyNext(QStringLiteral("InstalledApps"),
                         DelayedPebble::appsArguments(QStringLiteral("old-owner-app")));
    QTest::qWait(50);
    QCOMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("new-owner-app"));
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::compatibilityCommandsDoNotWaitAndPreserveArguments()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-compatibility-commands"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(pebble.installedApps()->rowCount(), 1);

    const QStringList commands = QStringList()
        << QStringLiteral("LoadLanguagePack") << QStringLiteral("ConfigurationClosed")
        << QStringLiteral("LaunchApp") << QStringLiteral("ConfigurationURL")
        << QStringLiteral("RemoveApp") << QStringLiteral("InstallApp")
        << QStringLiteral("SideloadApp") << QStringLiteral("SetAppOrder")
        << QStringLiteral("RequestScreenshot") << QStringLiteral("RemoveScreenshot")
        << QStringLiteral("PerformFirmwareUpgrade");
    foreach (const QString &command, commands) {
        watch.defer(command);
    }

    QElapsedTimer timer;
    timer.start();
    pebble.loadLanguagePack(QStringLiteral("/tmp/English.pbl"));
    pebble.configurationClosed(QStringLiteral("config-app"), QStringLiteral("pebblejs://close"));
    pebble.launchApp(QStringLiteral("launch-app"));
    pebble.requestConfigurationURL(QStringLiteral("configure-app"));
    pebble.removeApp(QStringLiteral("remove-app"));
    pebble.installApp(QStringLiteral("store-app"));
    pebble.sideloadApp(QStringLiteral("/tmp/local.pbw"));
    QVERIFY(QMetaObject::invokeMethod(&pebble, "appsSorted", Qt::DirectConnection));
    pebble.requestScreenshot();
    pebble.removeScreenshot(QStringLiteral("/tmp/screenshot.png"));
    pebble.performFirmwareUpgrade();
    const qint64 elapsed = timer.elapsed();

    foreach (const QString &command, commands) {
        QTRY_COMPARE(watch.pendingCount(command), 1);
    }
    QCOMPARE(watch.arguments(QStringLiteral("LoadLanguagePack")).at(0).toString(),
             QStringLiteral("/tmp/English.pbl"));
    QCOMPARE(watch.arguments(QStringLiteral("ConfigurationClosed")).at(0).toString(),
             QStringLiteral("config-app"));
    QCOMPARE(watch.arguments(QStringLiteral("ConfigurationClosed")).at(1).toString(),
             QStringLiteral("pebblejs://close"));
    QCOMPARE(watch.arguments(QStringLiteral("LaunchApp")).at(0).toString(),
             QStringLiteral("launch-app"));
    QCOMPARE(watch.arguments(QStringLiteral("ConfigurationURL")).at(0).toString(),
             QStringLiteral("configure-app"));
    QCOMPARE(watch.arguments(QStringLiteral("RemoveApp")).at(0).toString(),
             QStringLiteral("remove-app"));
    QCOMPARE(watch.arguments(QStringLiteral("InstallApp")).at(0).toString(),
             QStringLiteral("store-app"));
    QCOMPARE(watch.arguments(QStringLiteral("SideloadApp")).at(0).toString(),
             QStringLiteral("/tmp/local.pbw"));
    QCOMPARE(watch.arguments(QStringLiteral("SetAppOrder")).at(0).toStringList(),
             QStringList() << QStringLiteral("test-app"));
    QCOMPARE(watch.arguments(QStringLiteral("RemoveScreenshot")).at(0).toString(),
             QStringLiteral("/tmp/screenshot.png"));
    QVERIFY2(elapsed < 1000, "Compatibility commands waited for their D-Bus replies");

    foreach (const QString &command, commands) {
        watch.replyNext(command, QVariantList());
    }
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::compatibilityCommandErrorsDoNotSynthesizeState()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-compatibility-errors"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(pebble.installedApps()->rowCount(), 1);
    const int initialAppsRequests = watch.receivedCount(QStringLiteral("InstalledApps"));
    const int initialScreenshotsRequests = watch.receivedCount(QStringLiteral("Screenshots"));
    watch.defer(QStringLiteral("InstallApp"));
    watch.defer(QStringLiteral("RequestScreenshot"));
    watch.defer(QStringLiteral("PerformFirmwareUpgrade"));

    pebble.installApp(QStringLiteral("failed-install"));
    pebble.requestScreenshot();
    pebble.performFirmwareUpgrade();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("InstallApp")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("RequestScreenshot")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("PerformFirmwareUpgrade")), 1);
    watch.replyPendingError(QStringLiteral("InstallApp"), 0);
    watch.replyPendingError(QStringLiteral("RequestScreenshot"), 0);
    watch.replyPendingError(QStringLiteral("PerformFirmwareUpgrade"), 0);
    QTest::qWait(50);

    QCOMPARE(pebble.installedApps()->rowCount(), 1);
    QCOMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("baseline-app"));
    QVERIFY(pebble.screenshots()->latestScreenshot().isEmpty());
    QVERIFY(!pebble.firmwareUpgradeAvailable());
    QCOMPARE(watch.receivedCount(QStringLiteral("InstalledApps")), initialAppsRequests);
    QCOMPARE(watch.receivedCount(QStringLiteral("Screenshots")), initialScreenshotsRequests);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerCompatibilityCommandReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-compatibility-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.defer(QStringLiteral("InstallApp"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.installApp(QStringLiteral("old-owner-install"));
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("InstallApp")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-compatibility-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.setAppName(QStringLiteral("new-owner-app"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(pebble.installedApps()->rowCount(), 1);
    QTRY_COMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("new-owner-app"));
    const int currentOwnerAppsRequests = secondWatch.receivedCount(QStringLiteral("InstalledApps"));

    firstWatch.replyPendingError(QStringLiteral("InstallApp"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.installedApps()->get(0)->name(), QStringLiteral("new-owner-app"));
    QCOMPARE(secondWatch.receivedCount(QStringLiteral("InstalledApps")), currentOwnerAppsRequests);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oauthTokenCallbackParsing_data()
{
    QTest::addColumn<QString>("callback");
    QTest::addColumn<QString>("token");

    const QString prefix = QStringLiteral("pebble://custom-boot-config-url/");
    QTest::newRow("direct-padding")
        << prefix + QStringLiteral(
               "https://boot.rebble.io/api/config?access_token=YWJjZA==&t=1")
        << QStringLiteral("YWJjZA==");
    QTest::newRow("direct-encoded-ampersand")
        << prefix + QStringLiteral(
               "https://boot.rebble.io/api/config?access_token=left%26right&t=1")
        << QStringLiteral("left&right");
    QTest::newRow("direct-encoded-plus")
        << prefix + QStringLiteral(
               "https://boot.rebble.io/api/config?access_token=left%2Bright&t=1")
        << QStringLiteral("left+right");
    QTest::newRow("direct-encoded-slash")
        << prefix + QStringLiteral(
               "https://boot.rebble.io/api/config?access_token=left%2Fright&t=1")
        << QStringLiteral("left/right");
    QTest::newRow("outer-encoded-padding")
        << prefix + QStringLiteral(
               "https%3A%2F%2Fboot.rebble.io%2Fapi%2Fconfig%3Faccess_token%3D"
               "YWJjZA%3D%3D%26t%3D1")
        << QStringLiteral("YWJjZA==");
    QTest::newRow("outer-encoded-reserved-token")
        << prefix + QStringLiteral(
               "https%3A%2F%2Fboot.rebble.io%2Fapi%2Fconfig%3Faccess_token%3D"
               "left%2526middle%252Bright%252Ftail%26t%3D1")
        << QStringLiteral("left&middle+right/tail");
    QTest::newRow("malformed-short-percent")
        << prefix + QStringLiteral(
               "https://boot.rebble.io/api/config?access_token=broken%2&t=1")
        << QString();
    QTest::newRow("malformed-nonhex-percent")
        << prefix + QStringLiteral(
               "https%3A%2F%2Fboot.rebble.io%2Fapi%3Faccess_token%3Dbad%2X")
        << QString();
    QTest::newRow("empty-token")
        << prefix + QStringLiteral(
               "https://boot.rebble.io/api/config?access_token=&t=1")
        << QString();
    QTest::newRow("wrong-scheme")
        << QStringLiteral(
               "pebblejs://custom-boot-config-url/"
               "https://boot.rebble.io/?access_token=secret")
        << QString();
    QTest::newRow("wrong-action")
        << QStringLiteral(
               "pebble://close/https://boot.rebble.io/?access_token=secret")
        << QString();
}

void PebbleAsyncTest::oauthTokenCallbackParsing()
{
    QFETCH(QString, callback);
    QFETCH(QString, token);

    QCOMPARE(RockpoolAccount::oauthTokenFromCallback(callback), token);
}

void PebbleAsyncTest::oauthUsesPrimaryOperationAndRefreshesOnlyAtTerminal()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-primary"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);

    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);
    const QString operationPath = QStringLiteral("/io/rebble/libpebble3/Operations/oauth_primary");
    AccountTokenOperation operation(operationPath, connection);
    registerAccountOperation(&operation, operationPath, connection);

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();

    QElapsedTimer timer;
    timer.start();
    pebble.setOAuthToken(QStringLiteral("primary-token"));
    const qint64 elapsed = timer.elapsed();
    QVERIFY(pebble.accountTokenPending());
    QVERIFY(pebble.accountTokenError().isEmpty());
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    QCOMPARE(account.tokenArgument(0), QStringLiteral("primary-token"));
    QCOMPARE(watch.receivedCount(QStringLiteral("setOAuthToken")), 0);
    QVERIFY2(elapsed < 1000, "SetOAuthToken waited for its D-Bus reply");

    account.replyNextToken(connection, operationPath);
    QTRY_VERIFY(operation.getAllCount() >= 1);
    QTest::qWait(50);
    QVERIFY(pebble.accountTokenPending());
    QCOMPARE(account.receivedCount(), initialReads);

    account.setProperties(accountProperties(
        true, QStringLiteral("Primary Account"), QStringLiteral("primary@example.test")));
    operation.finish(true);
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);
    QTRY_VERIFY(pebble.accountAuthenticated());
    QCOMPARE(pebble.accountName(), QStringLiteral("Primary Account"));
    QVERIFY(pebble.accountTokenError().isEmpty());

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oauthOperationFailureIsExposed()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-operation-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);
    const QString operationPath = QStringLiteral("/io/rebble/libpebble3/Operations/oauth_failure");
    AccountTokenOperation operation(operationPath, connection);
    registerAccountOperation(&operation, operationPath, connection);

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();
    pebble.setOAuthToken(QStringLiteral("rejected-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    account.replyNextToken(connection, operationPath);
    QTRY_VERIFY(operation.getAllCount() >= 1);

    operation.finish(false, QStringLiteral("io.rebble.libpebble3.Error.IO"),
                     QStringLiteral("could not save account credentials"));
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(pebble.accountTokenError(),
                 QStringLiteral("could not save account credentials"));
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);
    QVERIFY(!pebble.accountAuthenticated());

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::olderOAuthOperationIsRetainedButCannotFinishLatestWrite()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-retained"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);
    const QString firstPath = QStringLiteral("/io/rebble/libpebble3/Operations/oauth_older");
    const QString secondPath = QStringLiteral("/io/rebble/libpebble3/Operations/oauth_latest");
    AccountTokenOperation firstOperation(firstPath, connection);
    AccountTokenOperation secondOperation(secondPath, connection);
    registerAccountOperation(&firstOperation, firstPath, connection);
    registerAccountOperation(&secondOperation, secondPath, connection);

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();
    pebble.setOAuthToken(QStringLiteral("older-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    account.replyNextToken(connection, firstPath);
    QTRY_VERIFY(firstOperation.getAllCount() >= 1);

    pebble.setOAuthToken(QStringLiteral("latest-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    account.replyNextToken(connection, secondPath);
    QTRY_VERIFY(secondOperation.getAllCount() >= 1);
    QTRY_COMPARE(pebble.findChildren<RockpoolOperation *>().count(), 2);

    firstOperation.finish(false, QStringLiteral("io.rebble.libpebble3.Error.IO"),
                          QStringLiteral("stale operation failed"));
    QTRY_COMPARE(pebble.findChildren<RockpoolOperation *>().count(), 1);
    QVERIFY(pebble.accountTokenPending());
    QVERIFY(pebble.accountTokenError().isEmpty());
    QCOMPARE(account.receivedCount(), initialReads);

    account.setProperties(accountProperties(
        true, QStringLiteral("Latest Account"), QStringLiteral("latest@example.test")));
    secondOperation.finish(true);
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);
    QTRY_VERIFY(pebble.accountAuthenticated());
    QVERIFY(pebble.accountTokenError().isEmpty());

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::staleOAuthMethodReplyIsIgnored()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-reordered"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);
    const QString stalePath = QStringLiteral("/io/rebble/libpebble3/Operations/oauth_stale_reply");
    const QString latestPath = QStringLiteral("/io/rebble/libpebble3/Operations/oauth_current_reply");
    AccountTokenOperation staleOperation(stalePath, connection);
    AccountTokenOperation latestOperation(latestPath, connection);
    registerAccountOperation(&staleOperation, stalePath, connection);
    registerAccountOperation(&latestOperation, latestPath, connection);

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();
    pebble.setOAuthToken(QStringLiteral("stale-token"));
    pebble.setOAuthToken(QStringLiteral("current-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 2);

    account.replyToken(connection, 0, stalePath);
    QTest::qWait(50);
    QCOMPARE(staleOperation.getAllCount(), 0);
    QCOMPARE(pebble.findChildren<RockpoolOperation *>().count(), 0);
    QVERIFY(pebble.accountTokenPending());
    QCOMPARE(account.receivedCount(), initialReads);

    account.replyNextToken(connection, latestPath);
    QTRY_VERIFY(latestOperation.getAllCount() >= 1);
    latestOperation.finish(true);
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);
    QVERIFY(pebble.accountTokenError().isEmpty());

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::accountOwnerReplacementRejectsOldOAuthWork()
{
    QDBusConnection rockpoolConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-owner-rockpool"));
    QVERIFY(rockpoolConnection.isConnected());
    DelayedPebble watch(rockpoolConnection);
    registerWatch(&watch, rockpoolConnection);
    registerService(rockpoolConnection);

    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-owner-old"));
    QVERIFY(firstConnection.isConnected());
    DelayedAccountProperties firstAccount(accountProperties());
    firstAccount.deferSetOAuthToken();
    registerAccountService(&firstAccount, firstConnection,
                           QDBusConnectionInterface::DontQueueService,
                           QDBusConnectionInterface::AllowReplacement);
    const QString oldOperationPath =
        QStringLiteral("/io/rebble/libpebble3/Operations/oauth_old_owner_operation");
    const QString lateReplyPath =
        QStringLiteral("/io/rebble/libpebble3/Operations/oauth_old_owner_reply");
    AccountTokenOperation oldOperation(oldOperationPath, firstConnection);
    AccountTokenOperation lateReplyOperation(lateReplyPath, firstConnection);
    registerAccountOperation(&oldOperation, oldOperationPath, firstConnection);
    registerAccountOperation(&lateReplyOperation, lateReplyPath, firstConnection);

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.setOAuthToken(QStringLiteral("old-owner-operation"));
    QTRY_COMPARE(firstAccount.tokenPendingCount(), 1);
    firstAccount.replyNextToken(firstConnection, oldOperationPath);
    QTRY_VERIFY(oldOperation.getAllCount() >= 1);
    pebble.setOAuthToken(QStringLiteral("old-owner-delayed-reply"));
    QTRY_COMPARE(firstAccount.tokenPendingCount(), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-owner-new"));
    QVERIFY(secondConnection.isConnected());
    DelayedAccountProperties secondAccount(accountProperties());
    secondAccount.deferSetOAuthToken();
    registerAccountService(&secondAccount, secondConnection,
                           QDBusConnectionInterface::ReplaceExistingService,
                           QDBusConnectionInterface::DontAllowReplacement);
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(pebble.accountTokenError(),
                 QStringLiteral("Rockpool account service changed"));
    QTRY_COMPARE(pebble.findChildren<RockpoolOperation *>().count(), 0);
    QTRY_VERIFY(secondAccount.receivedCount() >= 1);
    const int newOwnerReads = secondAccount.receivedCount();

    firstAccount.replyNextToken(firstConnection, lateReplyPath);
    QTest::qWait(50);
    QCOMPARE(lateReplyOperation.getAllCount(), 0);
    QCOMPARE(secondAccount.receivedCount(), newOwnerReads);

    const QString newOperationPath =
        QStringLiteral("/io/rebble/libpebble3/Operations/oauth_new_owner");
    AccountTokenOperation newOperation(newOperationPath, secondConnection);
    registerAccountOperation(&newOperation, newOperationPath, secondConnection);
    pebble.setOAuthToken(QStringLiteral("new-owner-token"));
    QTRY_COMPARE(secondAccount.tokenPendingCount(), 1);
    QVERIFY(pebble.accountTokenError().isEmpty());
    secondAccount.replyNextToken(secondConnection, newOperationPath);
    QTRY_VERIFY(newOperation.getAllCount() >= 1);
    secondAccount.setProperties(accountProperties(
        true, QStringLiteral("New Owner"), QStringLiteral("new-owner@example.test")));
    newOperation.finish(true);
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(secondAccount.receivedCount(), newOwnerReads + 1);
    QTRY_COMPARE(pebble.accountName(), QStringLiteral("New Owner"));
    QVERIFY(pebble.accountTokenError().isEmpty());

    QVERIFY(secondConnection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(rockpoolConnection.interface()->unregisterService(
                QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oauthTransportErrorIsExposed()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-transport-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);

    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();
    pebble.setOAuthToken(QStringLiteral("transport-error-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    QVERIFY(pebble.accountTokenPending());
    account.replyTokenError(connection, 0, QStringLiteral("transport rejected token"));
    QTRY_VERIFY(!pebble.accountTokenPending());
    QTRY_COMPARE(pebble.accountTokenError(), QStringLiteral("transport rejected token"));
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);
    QCOMPARE(watch.receivedCount(QStringLiteral("setOAuthToken")), 0);

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::pebbleFacadesShareLatestOAuthOperationState()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-shared-facades"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);
    const QString olderPath =
        QStringLiteral("/io/rebble/libpebble3/Operations/oauth_shared_older");
    const QString latestPath =
        QStringLiteral("/io/rebble/libpebble3/Operations/oauth_shared_latest");
    AccountTokenOperation olderOperation(olderPath, connection);
    AccountTokenOperation latestOperation(latestPath, connection);
    registerAccountOperation(&olderOperation, olderPath, connection);
    registerAccountOperation(&latestOperation, latestPath, connection);

    RockpoolAccount sharedAccount;
    Pebble first(QDBusObjectPath(QString::fromLatin1(watchPath)), 0, &sharedAccount);
    Pebble second(QDBusObjectPath(QString::fromLatin1(watchPath)), 0, &sharedAccount);
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();

    first.setOAuthToken(QStringLiteral("shared-older-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    account.replyNextToken(connection, olderPath);
    QTRY_VERIFY(olderOperation.getAllCount() >= 1);
    QVERIFY(first.accountTokenPending());
    QVERIFY(second.accountTokenPending());

    second.setOAuthToken(QStringLiteral("shared-latest-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    account.replyNextToken(connection, latestPath);
    QTRY_VERIFY(latestOperation.getAllCount() >= 1);
    QTRY_COMPARE(sharedAccount.findChildren<RockpoolOperation *>().count(), 2);

    olderOperation.finish(false, QStringLiteral("io.rebble.libpebble3.Error.IO"),
                          QStringLiteral("older shared operation failed"));
    QTRY_COMPARE(sharedAccount.findChildren<RockpoolOperation *>().count(), 1);
    QVERIFY(first.accountTokenPending());
    QVERIFY(second.accountTokenPending());
    QVERIFY(first.accountTokenError().isEmpty());
    QVERIFY(second.accountTokenError().isEmpty());
    QCOMPARE(account.receivedCount(), initialReads);

    latestOperation.finish(false, QStringLiteral("io.rebble.libpebble3.Error.IO"),
                           QStringLiteral("latest shared operation failed"));
    QTRY_VERIFY(!first.accountTokenPending());
    QTRY_VERIFY(!second.accountTokenPending());
    QTRY_COMPARE(first.accountTokenError(),
                 QStringLiteral("latest shared operation failed"));
    QCOMPARE(second.accountTokenError(), first.accountTokenError());
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::sharedOAuthOperationSurvivesPebbleReplacement()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-oauth-facade-replacement"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    DelayedAccountProperties account(accountProperties());
    account.deferSetOAuthToken();
    registerAccountService(&account, connection);
    const QString operationPath =
        QStringLiteral("/io/rebble/libpebble3/Operations/oauth_facade_replacement");
    AccountTokenOperation operation(operationPath, connection);
    registerAccountOperation(&operation, operationPath, connection);

    RockpoolAccount sharedAccount;
    QTRY_VERIFY(account.receivedCount() >= 1);
    const int initialReads = account.receivedCount();
    Pebble *first = new Pebble(
        QDBusObjectPath(QString::fromLatin1(watchPath)), 0, &sharedAccount);
    first->setOAuthToken(QStringLiteral("surviving-token"));
    QTRY_COMPARE(account.tokenPendingCount(), 1);
    account.replyNextToken(connection, operationPath);
    QTRY_VERIFY(operation.getAllCount() >= 1);
    QVERIFY(first->accountTokenPending());
    QTRY_COMPARE(sharedAccount.findChildren<RockpoolOperation *>().count(), 1);

    delete first;
    QVERIFY(sharedAccount.tokenPending());
    QCOMPARE(sharedAccount.findChildren<RockpoolOperation *>().count(), 1);

    Pebble replacement(
        QDBusObjectPath(QString::fromLatin1(watchPath)), 0, &sharedAccount);
    QVERIFY(replacement.accountTokenPending());
    QVERIFY(replacement.accountTokenError().isEmpty());
    account.setProperties(accountProperties(
        true, QStringLiteral("Replacement Facade"),
        QStringLiteral("replacement@example.test")));
    operation.finish(true);

    QTRY_VERIFY(!replacement.accountTokenPending());
    QTRY_COMPARE(account.receivedCount(), initialReads + 1);
    QTRY_VERIFY(replacement.accountAuthenticated());
    QCOMPARE(replacement.accountName(), QStringLiteral("Replacement Facade"));
    QCOMPARE(replacement.accountEmail(), QStringLiteral("replacement@example.test"));
    QVERIFY(replacement.accountTokenError().isEmpty());
    QTRY_COMPARE(sharedAccount.findChildren<RockpoolOperation *>().count(), 0);

    QVERIFY(connection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::accountOwnerAcquisitionRefreshesCachedValues()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-account-acquisition"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QVERIFY(!pebble.accountAuthenticated());
    QVERIFY(pebble.accountName().isEmpty());
    QVERIFY(pebble.accountEmail().isEmpty());

    QVariantMap account;
    account.insert(QStringLiteral("Authenticated"), true);
    account.insert(QStringLiteral("Name"), QStringLiteral("Pebble User"));
    account.insert(QStringLiteral("Email"), QStringLiteral("pebble@example.test"));
    DelayedAccountProperties properties(account);
    registerAccountService(&properties, connection);
    QTRY_COMPARE(properties.receivedCount(), 1);
    QTRY_VERIFY(pebble.accountAuthenticated());
    QTRY_COMPARE(pebble.accountName(), QStringLiteral("Pebble User"));
    QTRY_COMPARE(pebble.accountEmail(), QStringLiteral("pebble@example.test"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldAccountOwnerReplyIsIgnored()
{
    QDBusConnection rockpoolConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-account-rockpool"));
    QVERIFY(rockpoolConnection.isConnected());
    DelayedPebble watch(rockpoolConnection);
    registerWatch(&watch, rockpoolConnection);
    registerService(rockpoolConnection);

    QVariantMap oldAccount;
    oldAccount.insert(QStringLiteral("Authenticated"), true);
    oldAccount.insert(QStringLiteral("Name"), QStringLiteral("Old Account"));
    oldAccount.insert(QStringLiteral("Email"), QStringLiteral("old@example.test"));
    QDBusConnection firstAccountConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-account-old-owner"));
    QVERIFY(firstAccountConnection.isConnected());
    DelayedAccountProperties firstProperties(oldAccount);
    firstProperties.deferGetAll();
    registerAccountService(&firstProperties, firstAccountConnection,
                           QDBusConnectionInterface::DontQueueService,
                           QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(firstProperties.pendingCount(), 1);

    QVariantMap replacementAccount;
    replacementAccount.insert(QStringLiteral("Authenticated"), true);
    replacementAccount.insert(QStringLiteral("Name"), QStringLiteral("Replacement Account"));
    replacementAccount.insert(QStringLiteral("Email"), QStringLiteral("new@example.test"));
    QDBusConnection secondAccountConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-account-new-owner"));
    QVERIFY(secondAccountConnection.isConnected());
    DelayedAccountProperties secondProperties(replacementAccount);
    registerAccountService(&secondProperties, secondAccountConnection,
                           QDBusConnectionInterface::ReplaceExistingService,
                           QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(secondProperties.receivedCount(), 1);
    QTRY_VERIFY(pebble.accountAuthenticated());
    QTRY_COMPARE(pebble.accountName(), QStringLiteral("Replacement Account"));
    QTRY_COMPARE(pebble.accountEmail(), QStringLiteral("new@example.test"));

    firstProperties.replyNext(firstAccountConnection);
    QTest::qWait(50);
    QCOMPARE(pebble.accountName(), QStringLiteral("Replacement Account"));
    QCOMPARE(pebble.accountEmail(), QStringLiteral("new@example.test"));
    QVERIFY(secondAccountConnection.interface()->unregisterService(
                QString::fromLatin1(accountServiceName)).isValid());
    QVERIFY(rockpoolConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::accountOwnerLossClearsCachedValues()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-account-loss"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    QVariantMap account;
    account.insert(QStringLiteral("Authenticated"), true);
    account.insert(QStringLiteral("Name"), QStringLiteral("Connected Account"));
    account.insert(QStringLiteral("Email"), QStringLiteral("connected@example.test"));
    DelayedAccountProperties properties(account);
    registerAccountService(&properties, connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.accountAuthenticated());
    QTRY_COMPARE(pebble.accountName(), QStringLiteral("Connected Account"));

    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(accountServiceName)).isValid());
    QTRY_VERIFY(!pebble.accountAuthenticated());
    QTRY_VERIFY(pebble.accountName().isEmpty());
    QTRY_VERIFY(pebble.accountEmail().isEmpty());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::timelinePalettesLoadLazilyAndIndependently()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-palettes-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("TimelineColors"));
    watch.defer(QStringLiteral("TimelineIcons"));
    registerWatch(&watch, connection);
    registerService(connection);

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 constructionElapsed = timer.elapsed();

    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("TimelineColors")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("TimelineIcons")), 0);
    QVERIFY(pebble.timelineColors().isEmpty());
    QVERIFY(pebble.timelineIcons().isEmpty());
    QVERIFY(!pebble.timelineColorsReady());
    QVERIFY(!pebble.timelineIconsReady());

    timer.restart();
    pebble.refreshTimelineColors();
    const qint64 colorsElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineColors")), 1);
    QCOMPARE(watch.pendingCount(QStringLiteral("TimelineIcons")), 0);
    pebble.refreshTimelineColors();
    QTest::qWait(50);
    QCOMPARE(watch.pendingCount(QStringLiteral("TimelineColors")), 1);

    watch.replyNext(QStringLiteral("TimelineColors"),
                    DelayedPebble::timelinePaletteArguments(
                        timelinePalette(QStringLiteral("name"), QStringLiteral("Red"))));
    QTRY_VERIFY(pebble.timelineColorsReady());
    QCOMPARE(pebble.timelineColors().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Red"));
    QVERIFY(!pebble.timelineIconsReady());
    QCOMPARE(watch.receivedCount(QStringLiteral("TimelineIcons")), 0);

    timer.restart();
    pebble.refreshTimelineIcons();
    const qint64 iconsElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineIcons")), 1);
    watch.replyNext(QStringLiteral("TimelineIcons"),
                    DelayedPebble::timelinePaletteArguments(
                        timelinePalette(QStringLiteral("code"), QStringLiteral("TIMELINE_CALENDAR"))));
    QTRY_VERIFY(pebble.timelineIconsReady());
    QCOMPARE(pebble.timelineIcons().first().toMap().value(QStringLiteral("code")).toString(),
             QStringLiteral("TIMELINE_CALENDAR"));
    QCOMPARE(watch.receivedCount(QStringLiteral("TimelineColors")), 1);
    QVERIFY2(constructionElapsed < 1000,
             "Pebble construction issued blocking timeline palette reads");
    QVERIFY2(colorsElapsed < 1000,
             "TimelineColors refresh waited for its D-Bus reply");
    QVERIFY2(iconsElapsed < 1000,
             "TimelineIcons refresh waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedTimelinePaletteFetchesCanRetry()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-palettes-retry"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("TimelineColors"));
    watch.defer(QStringLiteral("TimelineIcons"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));

    pebble.refreshTimelineColors();
    pebble.refreshTimelineIcons();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineColors")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineIcons")), 1);
    watch.replyNext(QStringLiteral("TimelineColors"),
                    DelayedPebble::timelinePaletteArguments(
                        malformedTimelinePalette(QStringLiteral("name"),
                                                 QStringLiteral("Missing RGB"))));
    watch.replyNext(QStringLiteral("TimelineIcons"),
                    DelayedPebble::timelinePaletteArguments(
                        malformedTimelinePalette(QStringLiteral("code"),
                                                 QStringLiteral("MISSING_NAME"))));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineColors")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineIcons")), 1);
    QVERIFY(!pebble.timelineColorsReady());
    QVERIFY(!pebble.timelineIconsReady());
    QVERIFY(pebble.timelineColors().isEmpty());
    QVERIFY(pebble.timelineIcons().isEmpty());

    watch.replyNext(QStringLiteral("TimelineColors"), QVariantList());
    watch.replyPendingError(QStringLiteral("TimelineIcons"), 0);
    QTRY_VERIFY(pebble.timelineColorsReady());
    QTRY_VERIFY(pebble.timelineIconsReady());
    QVERIFY(pebble.timelineColors().isEmpty());
    QVERIFY(pebble.timelineIcons().isEmpty());

    pebble.refreshTimelineColors();
    pebble.refreshTimelineIcons();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineColors")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("TimelineIcons")), 1);
    watch.replyNext(QStringLiteral("TimelineColors"),
                    DelayedPebble::timelinePaletteArguments(
                        timelinePalette(QStringLiteral("name"), QStringLiteral("Blue"))));
    watch.replyNext(QStringLiteral("TimelineIcons"),
                    DelayedPebble::timelinePaletteArguments(
                        timelinePalette(QStringLiteral("code"), QStringLiteral("TIMELINE_MAIL"))));
    QTRY_VERIFY(pebble.timelineColorsReady());
    QTRY_VERIFY(pebble.timelineIconsReady());
    QCOMPARE(pebble.timelineColors().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Blue"));
    QCOMPARE(pebble.timelineIcons().first().toMap().value(QStringLiteral("code")).toString(),
             QStringLiteral("TIMELINE_MAIL"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerTimelinePaletteRepliesAreIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-palettes-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.defer(QStringLiteral("TimelineColors"));
    firstWatch.defer(QStringLiteral("TimelineIcons"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshTimelineColors();
    pebble.refreshTimelineIcons();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("TimelineColors")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("TimelineIcons")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-palettes-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.defer(QStringLiteral("TimelineColors"));
    secondWatch.defer(QStringLiteral("TimelineIcons"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("TimelineColors")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("TimelineIcons")), 1);
    QVERIFY(!pebble.timelineColorsReady());
    QVERIFY(!pebble.timelineIconsReady());
    secondWatch.replyNext(QStringLiteral("TimelineColors"),
                          DelayedPebble::timelinePaletteArguments(
                              timelinePalette(QStringLiteral("name"), QStringLiteral("New Red"))));
    secondWatch.replyNext(QStringLiteral("TimelineIcons"),
                          DelayedPebble::timelinePaletteArguments(
                              timelinePalette(QStringLiteral("code"), QStringLiteral("NEW_ICON"))));
    QTRY_VERIFY(pebble.timelineColorsReady());
    QTRY_VERIFY(pebble.timelineIconsReady());
    QCOMPARE(pebble.timelineColors().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("New Red"));
    QCOMPARE(pebble.timelineIcons().first().toMap().value(QStringLiteral("code")).toString(),
             QStringLiteral("NEW_ICON"));

    firstWatch.replyNext(QStringLiteral("TimelineColors"),
                         DelayedPebble::timelinePaletteArguments(
                             timelinePalette(QStringLiteral("name"), QStringLiteral("Old Red"))));
    firstWatch.replyNext(QStringLiteral("TimelineIcons"),
                         DelayedPebble::timelinePaletteArguments(
                             timelinePalette(QStringLiteral("code"), QStringLiteral("OLD_ICON"))));
    QTest::qWait(50);
    QCOMPARE(pebble.timelineColors().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("New Red"));
    QCOMPARE(pebble.timelineIcons().first().toMap().value(QStringLiteral("code")).toString(),
             QStringLiteral("NEW_ICON"));
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::notificationFilterCommandsDoNotWaitForReplies()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-notification-commands"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("NotificationsFilter")), 1);
    watch.defer(QStringLiteral("SetNotificationFilter"));
    watch.defer(QStringLiteral("ForgetNotificationFilter"));

    QElapsedTimer timer;
    timer.start();
    pebble.setNotificationFilter(QStringLiteral("set.source"), 2);
    pebble.forgetNotificationFilter(QStringLiteral("forget.source"));
    const qint64 elapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetNotificationFilter")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ForgetNotificationFilter")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("SetNotificationFilter")).at(0).toString(),
             QStringLiteral("set.source"));
    QCOMPARE(watch.arguments(QStringLiteral("SetNotificationFilter")).at(1).toInt(), 2);
    QCOMPARE(watch.arguments(QStringLiteral("ForgetNotificationFilter")).at(0).toString(),
             QStringLiteral("forget.source"));

    watch.replyNext(QStringLiteral("SetNotificationFilter"), QVariantList());
    watch.replyNext(QStringLiteral("ForgetNotificationFilter"), QVariantList());
    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("NotificationsFilter")), 1);
    QVERIFY2(elapsed < 1000, "Notification filter command waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::staleNotificationFilterCommandErrorIsIgnored()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-notification-command-order"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("NotificationsFilter")), 1);
    watch.defer(QStringLiteral("SetNotificationFilter"));
    watch.defer(QStringLiteral("ForgetNotificationFilter"));
    watch.defer(QStringLiteral("NotificationsFilter"));

    pebble.setNotificationFilter(QStringLiteral("same.source"), 0);
    pebble.forgetNotificationFilter(QStringLiteral("same.source"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetNotificationFilter")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ForgetNotificationFilter")), 1);

    watch.replyPendingError(QStringLiteral("SetNotificationFilter"), 0);
    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("NotificationsFilter")), 1);
    QCOMPARE(watch.pendingCount(QStringLiteral("NotificationsFilter")), 0);

    watch.replyPendingError(QStringLiteral("ForgetNotificationFilter"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("NotificationsFilter")), 1);
    QCOMPARE(watch.receivedCount(QStringLiteral("NotificationsFilter")), 2);
    watch.replyNext(QStringLiteral("NotificationsFilter"),
                    DelayedPebble::notificationArguments(QStringLiteral("canonical-source")));
    QTRY_COMPARE(pebble.notifications()->rowCount(), 1);
    QTRY_COMPARE(pebble.notifications()->data(pebble.notifications()->index(0),
                                               NotificationSourceModel::RoleName).toString(),
                 QStringLiteral("canonical-source"));
    QCOMPARE(pebble.notificationsFilter().value(QStringLiteral("test.source")).toMap()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("canonical-source"));
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerNotificationFilterCommandErrorIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus,
        QStringLiteral("pebble-async-notification-command-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.defer(QStringLiteral("SetNotificationFilter"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.setNotificationFilter(QStringLiteral("owner.source"), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("SetNotificationFilter")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus,
        QStringLiteral("pebble-async-notification-command-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(secondWatch.receivedCount(QStringLiteral("NotificationsFilter")), 1);
    QTRY_COMPARE(pebble.notifications()->rowCount(), 1);
    QTRY_COMPARE(pebble.notifications()->data(pebble.notifications()->index(0),
                                               NotificationSourceModel::RoleName).toString(),
                 QStringLiteral("baseline-source"));

    firstWatch.replyPendingError(QStringLiteral("SetNotificationFilter"), 0);
    QTest::qWait(50);
    QCOMPARE(secondWatch.receivedCount(QStringLiteral("NotificationsFilter")), 1);
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("NotificationsFilter")), 0);
    QCOMPARE(pebble.notifications()->data(pebble.notifications()->index(0),
                                          NotificationSourceModel::RoleName).toString(),
             QStringLiteral("baseline-source"));
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::settingsPageLoadsLazilyAndAtomically()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(true);
    watch.setProfileWhenConnectedValue(QStringLiteral("work"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("offline"));
    watch.setCalendarSyncEnabledValue(true);
    watch.setSyncAppsFromCloudValue(true);
    deferSettingsPage(&watch);
    registerWatch(&watch, connection);
    registerService(connection);

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 constructionElapsed = timer.elapsed();

    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("ImperialUnits")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("ProfileWhenConnected")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("ProfileWhenDisconnected")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("CalendarSyncEnabled")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("syncAppsFromCloud")), 0);
    QVERIFY(!pebble.settingsPageReady());
    QVERIFY(!pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QString());
    QCOMPARE(pebble.profileWhenDisconnected(), QString());
    QVERIFY(!pebble.calendarSyncEnabled());
    QVERIFY(!pebble.syncAppsFromCloud());
    watch.emitPebbleSignal(QStringLiteral("ImperialUnitsChanged"));
    watch.emitPebbleSignal(QStringLiteral("ProfileWhenConnectedChanged"));
    watch.emitPebbleSignal(QStringLiteral("ProfileWhenDisconnectedChanged"));
    watch.emitPebbleSignal(QStringLiteral("CalendarSyncEnabledChanged"));
    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("ImperialUnits")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("ProfileWhenConnected")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("ProfileWhenDisconnected")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("CalendarSyncEnabled")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("syncAppsFromCloud")), 0);

    timer.restart();
    pebble.refreshSettingsPage();
    const qint64 refreshElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenConnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("syncAppsFromCloud")), 1);

    watch.replyNext(QStringLiteral("ProfileWhenConnected"), QVariantList() << QStringLiteral("work"));
    QTRY_COMPARE(pebble.profileWhenConnected(), QStringLiteral("work"));
    QVERIFY(!pebble.settingsPageReady());
    watch.replyNext(QStringLiteral("CalendarSyncEnabled"), QVariantList() << true);
    QTRY_VERIFY(pebble.calendarSyncEnabled());
    QVERIFY(!pebble.settingsPageReady());
    watch.replyNext(QStringLiteral("ImperialUnits"), QVariantList() << true);
    QTRY_VERIFY(pebble.imperialUnits());
    QVERIFY(!pebble.settingsPageReady());
    watch.replyNext(QStringLiteral("ProfileWhenDisconnected"),
                    QVariantList() << QStringLiteral("offline"));
    QTRY_COMPARE(pebble.profileWhenDisconnected(), QStringLiteral("offline"));
    QVERIFY(!pebble.settingsPageReady());
    watch.replyNext(QStringLiteral("syncAppsFromCloud"), QVariantList() << true);
    QTRY_VERIFY(pebble.syncAppsFromCloud());
    QTRY_VERIFY(pebble.settingsPageReady());

    qDebug() << "settings-page construction elapsed" << constructionElapsed
             << "settings-page refresh elapsed" << refreshElapsed;
    QVERIFY2(constructionElapsed < 1000,
             "Pebble construction issued blocking Settings-page reads");
    QVERIFY2(refreshElapsed < 1000,
             "Settings-page refresh waited for D-Bus replies");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestSettingsPageReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-newest"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(false);
    watch.setProfileWhenConnectedValue(QStringLiteral("old-connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("old-disconnected"));
    watch.setCalendarSyncEnabledValue(false);
    watch.setSyncAppsFromCloudValue(false);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());

    deferSettingsPage(&watch);
    pebble.refreshSettingsPage();
    pebble.refreshSettingsPage();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenConnected")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("syncAppsFromCloud")), 2);

    watch.replyPending(QStringLiteral("ImperialUnits"), 1, QVariantList() << true);
    watch.replyPending(QStringLiteral("ProfileWhenConnected"), 1,
                       QVariantList() << QStringLiteral("new-connected"));
    watch.replyPending(QStringLiteral("ProfileWhenDisconnected"), 1,
                       QVariantList() << QStringLiteral("new-disconnected"));
    watch.replyPending(QStringLiteral("CalendarSyncEnabled"), 1, QVariantList() << true);
    watch.replyPending(QStringLiteral("syncAppsFromCloud"), 1, QVariantList() << true);
    QTRY_VERIFY(pebble.settingsPageReady());
    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(pebble.syncAppsFromCloud());

    replySettingsPage(&watch, false, QStringLiteral("old-connected"),
                      QStringLiteral("old-disconnected"), false);
    QTest::qWait(50);
    QVERIFY(pebble.settingsPageReady());
    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(pebble.syncAppsFromCloud());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::settingsPageSignalsRefreshNewestValues()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-signals"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(false);
    watch.setProfileWhenConnectedValue(QStringLiteral("old-connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("old-disconnected"));
    watch.setCalendarSyncEnabledValue(false);
    watch.setSyncAppsFromCloudValue(false);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());

    deferCoreSettingsPage(&watch);
    watch.emitPebbleSignal(QStringLiteral("ImperialUnitsChanged"));
    watch.emitPebbleSignal(QStringLiteral("ProfileWhenConnectedChanged"));
    watch.emitPebbleSignal(QStringLiteral("ProfileWhenDisconnectedChanged"));
    watch.emitPebbleSignal(QStringLiteral("CalendarSyncEnabledChanged"));
    watch.emitPebbleSignal(QStringLiteral("ImperialUnitsChanged"));
    watch.emitPebbleSignal(QStringLiteral("ProfileWhenConnectedChanged"));
    watch.emitPebbleSignal(QStringLiteral("ProfileWhenDisconnectedChanged"));
    watch.emitPebbleSignal(QStringLiteral("CalendarSyncEnabledChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenConnected")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 2);

    watch.replyPending(QStringLiteral("ImperialUnits"), 1, QVariantList() << true);
    watch.replyPending(QStringLiteral("ProfileWhenConnected"), 1,
                       QVariantList() << QStringLiteral("new-connected"));
    watch.replyPending(QStringLiteral("ProfileWhenDisconnected"), 1,
                       QVariantList() << QStringLiteral("new-disconnected"));
    watch.replyPending(QStringLiteral("CalendarSyncEnabled"), 1, QVariantList() << true);
    QTRY_VERIFY(pebble.imperialUnits());
    QTRY_COMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QTRY_COMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QTRY_VERIFY(pebble.calendarSyncEnabled());

    replyCoreSettingsPage(&watch, false, QStringLiteral("old-connected"),
                          QStringLiteral("old-disconnected"), false);
    QTest::qWait(50);
    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerSettingsPageReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    deferSettingsPage(&firstWatch);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("ProfileWhenConnected")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("syncAppsFromCloud")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.setImperialUnitsValue(true);
    secondWatch.setProfileWhenConnectedValue(QStringLiteral("new-connected"));
    secondWatch.setProfileWhenDisconnectedValue(QStringLiteral("new-disconnected"));
    secondWatch.setCalendarSyncEnabledValue(true);
    secondWatch.setSyncAppsFromCloudValue(true);
    deferSettingsPage(&secondWatch);
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("ProfileWhenConnected")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("syncAppsFromCloud")), 1);
    QVERIFY(!pebble.settingsPageReady());
    replySettingsPage(&secondWatch, true, QStringLiteral("new-connected"),
                      QStringLiteral("new-disconnected"), true, true);
    QTRY_VERIFY(pebble.settingsPageReady());
    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(pebble.syncAppsFromCloud());

    replySettingsPage(&firstWatch, false, QStringLiteral("old-connected"),
                      QStringLiteral("old-disconnected"), false);
    QTest::qWait(50);
    QVERIFY(pebble.settingsPageReady());
    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(pebble.syncAppsFromCloud());
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::settingsPageSettersDoNotWaitAndRemainIndependent()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-setters"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(false);
    watch.setProfileWhenConnectedValue(QStringLiteral("old-connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("old-disconnected"));
    watch.setCalendarSyncEnabledValue(false);
    watch.setSyncAppsFromCloudValue(false);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());

    watch.defer(QStringLiteral("SetImperialUnits"));
    watch.defer(QStringLiteral("SetProfileWhenConnected"));
    watch.defer(QStringLiteral("SetProfileWhenDisconnected"));
    watch.defer(QStringLiteral("SetCalendarSyncEnabled"));
    watch.defer(QStringLiteral("setSyncAppsFromCloud"));
    deferSettingsPage(&watch);
    QElapsedTimer timer;
    timer.start();
    pebble.setImperialUnits(true);
    pebble.setProfileWhenConnected(QStringLiteral("new-connected"));
    pebble.setProfileWhenDisconnected(QStringLiteral("new-disconnected"));
    pebble.setCalendarSyncEnabled(true);
    pebble.setSyncAppsFromCloud(true);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(pebble.syncAppsFromCloud());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetImperialUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetProfileWhenConnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetProfileWhenDisconnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetCalendarSyncEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setSyncAppsFromCloud")), 1);
    QVERIFY(watch.arguments(QStringLiteral("SetImperialUnits")).first().toBool());
    QCOMPARE(watch.arguments(QStringLiteral("SetProfileWhenConnected")).first().toString(),
             QStringLiteral("new-connected"));
    QCOMPARE(watch.arguments(QStringLiteral("SetProfileWhenDisconnected")).first().toString(),
             QStringLiteral("new-disconnected"));
    QVERIFY(watch.arguments(QStringLiteral("SetCalendarSyncEnabled")).first().toBool());
    QVERIFY(watch.arguments(QStringLiteral("setSyncAppsFromCloud")).first().toBool());

    watch.replyNext(QStringLiteral("SetProfileWhenDisconnected"), QVariantList());
    watch.replyNext(QStringLiteral("SetCalendarSyncEnabled"), QVariantList());
    watch.replyNext(QStringLiteral("SetImperialUnits"), QVariantList());
    watch.replyNext(QStringLiteral("SetProfileWhenConnected"), QVariantList());
    watch.replyNext(QStringLiteral("setSyncAppsFromCloud"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenConnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("syncAppsFromCloud")), 1);
    replySettingsPage(&watch, true, QStringLiteral("new-connected"),
                      QStringLiteral("new-disconnected"), true, true);
    QTRY_VERIFY(pebble.imperialUnits());
    QTRY_COMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QTRY_COMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QTRY_VERIFY(pebble.calendarSyncEnabled());
    QTRY_VERIFY(pebble.syncAppsFromCloud());
    QVERIFY2(elapsed < 1000, "Settings-page setter waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedSettingsPageSettersRollBackAndReadBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-write-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(false);
    watch.setProfileWhenConnectedValue(QStringLiteral("old-connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("old-disconnected"));
    watch.setCalendarSyncEnabledValue(false);
    watch.setSyncAppsFromCloudValue(false);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());

    watch.defer(QStringLiteral("SetImperialUnits"));
    watch.defer(QStringLiteral("SetProfileWhenConnected"));
    watch.defer(QStringLiteral("SetProfileWhenDisconnected"));
    watch.defer(QStringLiteral("SetCalendarSyncEnabled"));
    watch.defer(QStringLiteral("setSyncAppsFromCloud"));
    deferSettingsPage(&watch);
    QElapsedTimer timer;
    timer.start();
    pebble.setImperialUnits(true);
    pebble.setProfileWhenConnected(QStringLiteral("new-connected"));
    pebble.setProfileWhenDisconnected(QStringLiteral("new-disconnected"));
    pebble.setCalendarSyncEnabled(true);
    pebble.setSyncAppsFromCloud(true);
    const qint64 elapsed = timer.elapsed();
    QVERIFY(pebble.imperialUnits());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("new-connected"));
    QCOMPARE(pebble.profileWhenDisconnected(), QStringLiteral("new-disconnected"));
    QVERIFY(pebble.calendarSyncEnabled());
    QVERIFY(pebble.syncAppsFromCloud());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetImperialUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetProfileWhenConnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetProfileWhenDisconnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetCalendarSyncEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setSyncAppsFromCloud")), 1);

    watch.setImperialUnitsValue(false);
    watch.setProfileWhenConnectedValue(QStringLiteral("old-connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("old-disconnected"));
    watch.setCalendarSyncEnabledValue(false);
    watch.setSyncAppsFromCloudValue(false);
    watch.replyPendingError(QStringLiteral("SetImperialUnits"), 0);
    watch.replyPendingError(QStringLiteral("SetProfileWhenConnected"), 0);
    watch.replyPendingError(QStringLiteral("SetProfileWhenDisconnected"), 0);
    watch.replyPendingError(QStringLiteral("SetCalendarSyncEnabled"), 0);
    watch.replyPendingError(QStringLiteral("setSyncAppsFromCloud"), 0);
    QTRY_VERIFY(!pebble.imperialUnits());
    QTRY_COMPARE(pebble.profileWhenConnected(), QStringLiteral("old-connected"));
    QTRY_COMPARE(pebble.profileWhenDisconnected(), QStringLiteral("old-disconnected"));
    QTRY_VERIFY(!pebble.calendarSyncEnabled());
    QTRY_VERIFY(!pebble.syncAppsFromCloud());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenConnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("syncAppsFromCloud")), 1);
    replySettingsPage(&watch, false, QStringLiteral("old-connected"),
                      QStringLiteral("old-disconnected"), false, false);
    QTRY_VERIFY(!pebble.imperialUnits());
    QTRY_COMPARE(pebble.profileWhenConnected(), QStringLiteral("old-connected"));
    QTRY_COMPARE(pebble.profileWhenDisconnected(), QStringLiteral("old-disconnected"));
    QTRY_VERIFY(!pebble.calendarSyncEnabled());
    QTRY_VERIFY(!pebble.syncAppsFromCloud());
    QVERIFY2(elapsed < 1000, "Settings-page setter waited for a failed D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::staleSettingsPageSetterRepliesAreIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-stale-write"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.setImperialUnitsValue(false);
    firstWatch.setProfileWhenConnectedValue(QStringLiteral("initial"));
    firstWatch.setProfileWhenDisconnectedValue(QStringLiteral("initial-disconnected"));
    firstWatch.setCalendarSyncEnabledValue(false);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());

    firstWatch.defer(QStringLiteral("SetProfileWhenConnected"));
    firstWatch.defer(QStringLiteral("ProfileWhenConnected"));
    pebble.setProfileWhenConnected(QStringLiteral("first-write"));
    pebble.setProfileWhenConnected(QStringLiteral("second-write"));
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("SetProfileWhenConnected")), 2);
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("second-write"));

    firstWatch.replyPendingError(QStringLiteral("SetProfileWhenConnected"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("second-write"));
    QCOMPARE(firstWatch.pendingCount(QStringLiteral("ProfileWhenConnected")), 0);
    QCOMPARE(firstWatch.pendingCount(QStringLiteral("SetProfileWhenConnected")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus,
        QStringLiteral("pebble-async-settings-page-stale-write-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.setImperialUnitsValue(true);
    secondWatch.setProfileWhenConnectedValue(QStringLiteral("replacement-owner"));
    secondWatch.setProfileWhenDisconnectedValue(QStringLiteral("replacement-disconnected"));
    secondWatch.setCalendarSyncEnabledValue(true);
    secondWatch.setSyncAppsFromCloudValue(true);
    deferSettingsPage(&secondWatch);
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("ProfileWhenConnected")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("ProfileWhenDisconnected")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("CalendarSyncEnabled")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("syncAppsFromCloud")), 1);
    replySettingsPage(&secondWatch, true, QStringLiteral("replacement-owner"),
                      QStringLiteral("replacement-disconnected"), true, true);
    QTRY_VERIFY(pebble.settingsPageReady());
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("replacement-owner"));
    QVERIFY(pebble.syncAppsFromCloud());

    firstWatch.replyPendingError(QStringLiteral("SetProfileWhenConnected"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.profileWhenConnected(), QStringLiteral("replacement-owner"));
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("ProfileWhenConnected")), 0);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::settingsSignalMakesCachedValueWritable()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-stale-value"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(true);
    watch.setProfileWhenConnectedValue(QStringLiteral("connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("disconnected"));
    watch.setCalendarSyncEnabledValue(true);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());
    QVERIFY(pebble.imperialUnits());

    watch.defer(QStringLiteral("ImperialUnits"));
    watch.defer(QStringLiteral("SetImperialUnits"));
    watch.emitPebbleSignal(QStringLiteral("ImperialUnitsChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QVERIFY(!pebble.settingsPageReady());

    // The cached value is still true, but the signal made it non-authoritative. A direct
    // setter call models choosing the unchanged value once the QML control becomes enabled.
    pebble.setImperialUnits(true);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetImperialUnits")), 1);
    QVERIFY(watch.arguments(QStringLiteral("SetImperialUnits")).first().toBool());

    watch.replyNext(QStringLiteral("SetImperialUnits"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 2);
    watch.replyPending(QStringLiteral("ImperialUnits"), 1, QVariantList() << true);
    QTRY_VERIFY(pebble.settingsPageReady());
    QVERIFY(pebble.imperialUnits());

    watch.replyNext(QStringLiteral("ImperialUnits"), QVariantList() << false);
    QTest::qWait(50);
    QVERIFY(pebble.imperialUnits());
    QVERIFY(pebble.settingsPageReady());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedSettingsReadFallsBackAndMakesValueWritable()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-settings-page-fallback"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setImperialUnitsValue(false);
    watch.setProfileWhenConnectedValue(QStringLiteral("connected"));
    watch.setProfileWhenDisconnectedValue(QStringLiteral("disconnected"));
    watch.setCalendarSyncEnabledValue(false);
    watch.defer(QStringLiteral("ImperialUnits"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));

    pebble.refreshSettingsPage();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QVERIFY(!pebble.settingsPageReady());
    watch.replyPendingError(QStringLiteral("ImperialUnits"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QVERIFY(!pebble.settingsPageReady());
    watch.replyPendingError(QStringLiteral("ImperialUnits"), 0);
    QTRY_VERIFY(pebble.settingsPageReady());
    QVERIFY(!pebble.imperialUnits());

    // The twice-failed false cache is fallback-only, so selecting false must not hit the
    // equality short-circuit. The fake immediately accepts the write, then holds its readback.
    pebble.setImperialUnits(false);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("SetImperialUnits")), 1);
    QVERIFY(!watch.arguments(QStringLiteral("SetImperialUnits")).first().toBool());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    watch.replyNext(QStringLiteral("ImperialUnits"), QVariantList() << false);
    QTRY_VERIFY(pebble.settingsPageReady());
    QVERIFY(!pebble.imperialUnits());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::healthParamsLoadLazilyAndDecodeDbusVariantMap()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("HealthParams"));
    registerWatch(&watch, connection);
    registerService(connection);
    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QVERIFY2(timer.elapsed() < 1000, "Pebble construction waited for HealthParams");
    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("HealthParams")), 0);
    QVERIFY(pebble.healthParams().isEmpty());
    QVERIFY(!pebble.healthParamsReady());

    pebble.refreshHealthParams();
    pebble.refreshHealthParams();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 2);
    const QVariantMap newest = healthParams(true, 41, QStringLiteral("female"),
                                            178, 72, true, false);
    watch.replyPending(QStringLiteral("HealthParams"), 1,
                       DelayedPebble::healthParamsArguments(newest));
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), newest);
    QCOMPARE(pebble.healthParams().value(QStringLiteral("enabled")).type(), QVariant::Bool);
    QCOMPARE(pebble.healthParams().value(QStringLiteral("age")).type(), QVariant::Int);
    QCOMPARE(pebble.healthParams().value(QStringLiteral("gender")).type(), QVariant::String);
    watch.replyNext(QStringLiteral("HealthParams"),
                    DelayedPebble::healthParamsArguments(healthParams(
                        false, 20, QStringLiteral("male"), 160, 50, false, true)));
    QTest::qWait(50);
    QCOMPARE(pebble.healthParams(), newest);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestHealthParamsReplyWinsAfterSignal()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-signal"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = healthParams(false, 30, QStringLiteral("male"),
                                             170, 70, false, false);
    watch.setHealthParamsValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_VERIFY(pebble.healthParamsReady());

    watch.defer(QStringLiteral("HealthParams"));
    watch.emitPebbleSignal(QStringLiteral("HealthParamsChanged"));
    watch.emitPebbleSignal(QStringLiteral("HealthParamsChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 2);
    QVERIFY(!pebble.healthParamsReady());
    const QVariantMap newest = healthParams(true, 45, QStringLiteral("female"),
                                            181, 73, true, true);
    watch.replyPending(QStringLiteral("HealthParams"), 1,
                       DelayedPebble::healthParamsArguments(newest));
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), newest);
    watch.replyNext(QStringLiteral("HealthParams"),
                    DelayedPebble::healthParamsArguments(initial));
    QTest::qWait(50);
    QCOMPARE(pebble.healthParams(), newest);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::healthParamsWriteDoesNotWaitAndReadsBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap oldParams = healthParams(false, 30, QStringLiteral("male"),
                                               170, 70, false, false);
    const QVariantMap newParams = healthParams(true, 31, QStringLiteral("female"),
                                               171, 71, true, false);
    watch.setHealthParamsValue(oldParams);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_VERIFY(pebble.healthParamsReady());

    watch.defer(QStringLiteral("SetHealthParams"));
    watch.defer(QStringLiteral("HealthParams"));
    QElapsedTimer timer;
    timer.start();
    pebble.setHealthParams(newParams);
    QVERIFY2(timer.elapsed() < 1000, "SetHealthParams waited for its D-Bus reply");
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetHealthParams")), 1);
    QCOMPARE(decodedHealthParams(
                 watch.arguments(QStringLiteral("SetHealthParams")).first()), newParams);
    QVERIFY(!pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), newParams);

    watch.setHealthParamsValue(newParams);
    watch.replyNext(QStringLiteral("SetHealthParams"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyNext(QStringLiteral("HealthParams"),
                    DelayedPebble::healthParamsArguments(newParams));
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), newParams);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::staleHealthParamsWriteErrorIsIgnored()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-stale-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = healthParams(false, 30, QStringLiteral("male"),
                                             170, 70, false, false);
    const QVariantMap first = healthParams(true, 31, QStringLiteral("male"),
                                           170, 70, false, false);
    const QVariantMap second = healthParams(true, 32, QStringLiteral("female"),
                                            171, 71, true, true);
    watch.setHealthParamsValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_VERIFY(pebble.healthParamsReady());

    watch.defer(QStringLiteral("SetHealthParams"));
    watch.defer(QStringLiteral("HealthParams"));
    pebble.setHealthParams(first);
    pebble.setHealthParams(second);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetHealthParams")), 2);
    watch.replyPendingError(QStringLiteral("SetHealthParams"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.healthParams(), second);
    QCOMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 0);

    watch.setHealthParamsValue(second);
    watch.replyNext(QStringLiteral("SetHealthParams"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyNext(QStringLiteral("HealthParams"),
                    DelayedPebble::healthParamsArguments(second));
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), second);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerHealthParamsReplyAndErrorAreIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    const QVariantMap firstParams = healthParams(false, 30, QStringLiteral("male"),
                                                 170, 70, false, false);
    firstWatch.setHealthParamsValue(firstParams);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_VERIFY(pebble.healthParamsReady());

    firstWatch.defer(QStringLiteral("HealthParams"));
    firstWatch.defer(QStringLiteral("SetHealthParams"));
    pebble.refreshHealthParams();
    pebble.setHealthParams(healthParams(true, 31, QStringLiteral("female"),
                                         171, 71, true, false));
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("HealthParams")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("SetHealthParams")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    const QVariantMap replacement = healthParams(true, 45, QStringLiteral("female"),
                                                 181, 73, true, true);
    secondWatch.setHealthParamsValue(replacement);
    secondWatch.defer(QStringLiteral("HealthParams"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("HealthParams")), 1);
    secondWatch.replyNext(QStringLiteral("HealthParams"),
                          DelayedPebble::healthParamsArguments(replacement));
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), replacement);

    firstWatch.replyNext(QStringLiteral("HealthParams"),
                         DelayedPebble::healthParamsArguments(firstParams));
    firstWatch.replyPendingError(QStringLiteral("SetHealthParams"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.healthParams(), replacement);
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("HealthParams")), 0);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::coldHealthParamsReadFailureRemainsNotReady()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-cold-failure"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("HealthParams"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyPendingError(QStringLiteral("HealthParams"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyPendingError(QStringLiteral("HealthParams"), 0);
    QTest::qWait(50);
    QVERIFY(!pebble.healthParamsReady());
    QVERIFY(pebble.healthParams().isEmpty());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::retainedHealthParamsFallbackRemainsWritable()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-fallback"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap params = healthParams(false, 30, QStringLiteral("male"),
                                            170, 70, false, false);
    watch.setHealthParamsValue(params);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_VERIFY(pebble.healthParamsReady());

    watch.defer(QStringLiteral("HealthParams"));
    pebble.refreshHealthParams();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyPendingError(QStringLiteral("HealthParams"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyPendingError(QStringLiteral("HealthParams"), 0);
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(pebble.healthParams(), params);

    watch.defer(QStringLiteral("SetHealthParams"));
    pebble.setHealthParams(params);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetHealthParams")), 1);
    watch.replyNext(QStringLiteral("SetHealthParams"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    watch.replyNext(QStringLiteral("HealthParams"), DelayedPebble::healthParamsArguments(params));
    QTRY_VERIFY(pebble.healthParamsReady());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::healthParamsAndImperialUnitsRemainIndependent()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-units"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setHealthParamsValue(healthParams(true, 40, QStringLiteral("female"),
                                             175, 65, true, false));
    watch.setImperialUnitsValue(true);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthParams();
    QTRY_VERIFY(pebble.healthParamsReady());
    QCOMPARE(watch.receivedCount(QStringLiteral("ImperialUnits")), 0);

    pebble.refreshSettingsPage();
    QTRY_VERIFY(pebble.settingsPageReady());
    QCOMPARE(watch.receivedCount(QStringLiteral("HealthParams")), 1);
    watch.defer(QStringLiteral("HealthParams"));
    watch.emitPebbleSignal(QStringLiteral("HealthParamsChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 1);
    QCOMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 0);
    watch.replyNext(QStringLiteral("HealthParams"), DelayedPebble::healthParamsArguments(
                        healthParams(true, 40, QStringLiteral("female"), 175, 65, true, false)));

    watch.defer(QStringLiteral("ImperialUnits"));
    watch.emitPebbleSignal(QStringLiteral("ImperialUnitsChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("ImperialUnits")), 1);
    QCOMPARE(watch.pendingCount(QStringLiteral("HealthParams")), 0);
    watch.replyNext(QStringLiteral("ImperialUnits"), QVariantList() << false);
    QTRY_VERIFY(!pebble.imperialUnits());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::healthOverviewLoadsLazilyAndDecodesNestedDbusValues()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-overview-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("HealthOverview"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("HealthOverview")), 0);
    QVERIFY(pebble.healthOverview().isEmpty());
    QVERIFY(!pebble.healthOverviewReady());

    pebble.refreshHealthOverview();
    pebble.refreshHealthOverview();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthOverview")), 2);
    const QVariantMap newest = healthOverview(9000, QStringLiteral("new-"));
    watch.replyPending(QStringLiteral("HealthOverview"), 1,
                       DelayedPebble::healthOverviewArguments(newest));
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), newest);
    QCOMPARE(pebble.healthOverview().value(QStringLiteral("todaySteps")).type(), QVariant::Int);
    QCOMPARE(pebble.healthOverview().value(QStringLiteral("stepsWeek")).type(), QVariant::List);
    const QVariantMap firstDay = pebble.healthOverview().value(
        QStringLiteral("stepsWeek")).toList().first().toMap();
    QCOMPARE(firstDay.value(QStringLiteral("steps")).type(), QVariant::Int);
    QCOMPARE(firstDay.value(QStringLiteral("label")).type(), QVariant::String);
    watch.replyNext(QStringLiteral("HealthOverview"),
                    DelayedPebble::healthOverviewArguments(
                        healthOverview(100, QStringLiteral("old-"))));
    QTest::qWait(50);
    QCOMPARE(pebble.healthOverview(), newest);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::healthDataChangedRefreshesNewestOverview()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-overview-signal"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = healthOverview(1000, QStringLiteral("initial-"));
    watch.setHealthOverviewValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthOverview();
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), initial);

    watch.defer(QStringLiteral("HealthOverview"));
    watch.emitPebbleSignal(QStringLiteral("HealthDataChanged"));
    watch.emitPebbleSignal(QStringLiteral("HealthDataChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthOverview")), 2);
    QVERIFY(!pebble.healthOverviewReady());
    const QVariantMap newest = healthOverview(8000, QStringLiteral("signal-"));
    watch.replyPending(QStringLiteral("HealthOverview"), 1,
                       DelayedPebble::healthOverviewArguments(newest));
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), newest);
    watch.replyNext(QStringLiteral("HealthOverview"),
                    DelayedPebble::healthOverviewArguments(initial));
    QTest::qWait(50);
    QCOMPARE(pebble.healthOverview(), newest);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerHealthOverviewReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-overview-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.defer(QStringLiteral("HealthOverview"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthOverview();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("HealthOverview")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-overview-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    const QVariantMap replacement = healthOverview(6000, QStringLiteral("replacement-"));
    secondWatch.defer(QStringLiteral("HealthOverview"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("HealthOverview")), 1);
    secondWatch.replyNext(QStringLiteral("HealthOverview"),
                          DelayedPebble::healthOverviewArguments(replacement));
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), replacement);

    firstWatch.replyNext(QStringLiteral("HealthOverview"),
                         DelayedPebble::healthOverviewArguments(
                             healthOverview(20, QStringLiteral("old-"))));
    QTest::qWait(50);
    QCOMPARE(pebble.healthOverview(), replacement);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::fetchHealthDataDoesNotWaitAndRefreshesOverview()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-fetch-health-data"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setHealthOverviewValue(healthOverview(1000, QStringLiteral("before-")));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthOverview();
    QTRY_VERIFY(pebble.healthOverviewReady());

    watch.defer(QStringLiteral("FetchHealthData"));
    watch.defer(QStringLiteral("HealthOverview"));
    QElapsedTimer timer;
    timer.start();
    pebble.fetchHealthData();
    QVERIFY2(timer.elapsed() < 1000, "FetchHealthData waited for its D-Bus reply");
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("FetchHealthData")), 1);
    QVERIFY(pebble.healthSyncing());
    pebble.fetchHealthData();
    QCOMPARE(watch.pendingCount(QStringLiteral("FetchHealthData")), 1);

    const QVariantMap refreshed = healthOverview(9000, QStringLiteral("after-"));
    watch.replyNext(QStringLiteral("FetchHealthData"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthOverview")), 1);
    watch.replyNext(QStringLiteral("HealthOverview"),
                    DelayedPebble::healthOverviewArguments(refreshed));
    QTRY_VERIFY(!pebble.healthSyncing());
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), refreshed);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::fetchHealthDataErrorCompletesOnce()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-fetch-health-data-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QSignalSpy completed(&pebble,
                         QMetaMethod::fromSignal(&Pebble::healthSyncCompleted));
    watch.defer(QStringLiteral("FetchHealthData"));

    pebble.fetchHealthData();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("FetchHealthData")), 1);
    QVERIFY(pebble.healthSyncing());
    watch.replyPendingError(QStringLiteral("FetchHealthData"), 0);
    QTRY_VERIFY(!pebble.healthSyncing());
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(completed.takeFirst().at(0).toBool(), false);
    QTest::qWait(50);
    QCOMPARE(completed.count(), 0);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedHealthOverviewRetainsValidatedSnapshot()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-overview-failure"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap snapshot = healthOverview(4000, QStringLiteral("valid-"));
    watch.setHealthOverviewValue(snapshot);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthOverview();
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), snapshot);

    watch.defer(QStringLiteral("HealthOverview"));
    pebble.refreshHealthOverview();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthOverview")), 1);
    QVERIFY(!pebble.healthOverviewReady());
    watch.replyPendingError(QStringLiteral("HealthOverview"), 0);
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), snapshot);

    pebble.refreshHealthOverview();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("HealthOverview")), 1);
    QVariantMap invalid;
    invalid.insert(QStringLiteral("todaySteps"), 1);
    watch.replyNext(QStringLiteral("HealthOverview"),
                    DelayedPebble::healthOverviewArguments(invalid));
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), snapshot);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::healthHistoryValidatesDatesAndPresence()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-health-history"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    QVariantMap snapshot = healthOverview(4000, QStringLiteral("valid-"));
    QVariantList history;
    for (int i = 0; i < 90; ++i) {
        QVariantMap day;
        day.insert(QStringLiteral("date"), QDate(2024, 1, 1).addDays(i).toString(Qt::ISODate));
        day.insert(QStringLiteral("steps"), i);
        day.insert(QStringLiteral("sleepDuration"), 0);
        day.insert(QStringLiteral("deepSleepDuration"), 0);
        day.insert(QStringLiteral("hasMovement"), 1);
        day.insert(QStringLiteral("hasSleep"), 0);
        history.append(day);
    }
    snapshot.insert(QStringLiteral("history"), history);
    watch.setHealthOverviewValue(snapshot);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshHealthOverview();
    QTRY_VERIFY(pebble.healthOverviewReady());
    QCOMPARE(pebble.healthOverview(), snapshot);

    QList<QVariantList> invalidHistories;
    QVariantList invalid = history;
    QVariantMap day = invalid[1].toMap();
    day.insert(QStringLiteral("date"), QStringLiteral("2024-01-04"));
    invalid[1] = day;
    invalidHistories.append(invalid);
    invalid = history;
    day = invalid[0].toMap();
    day.insert(QStringLiteral("hasSleep"), 2);
    invalid[0] = day;
    invalidHistories.append(invalid);
    invalid = history;
    invalid.append(history.last());
    invalidHistories.append(invalid);
    invalidHistories.append(QVariantList());
    foreach (const QVariantList &badHistory, invalidHistories) {
        QVariantMap bad = snapshot;
        bad.insert(QStringLiteral("todaySteps"), 999);
        bad.insert(QStringLiteral("history"), badHistory);
        watch.setHealthOverviewValue(bad);
        pebble.refreshHealthOverview();
        QTRY_VERIFY(pebble.healthOverviewReady());
        QCOMPARE(pebble.healthOverview(), snapshot);
    }
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::cannedResponsesLoadLazilyAndUseCachedGetter()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap responses = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("yes") << QStringLiteral("no"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("later"));
    watch.setCannedResponsesValue(responses);
    watch.defer(QStringLiteral("cannedResponses"));
    registerWatch(&watch, connection);
    registerService(connection);

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 constructionElapsed = timer.elapsed();

    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("cannedResponses")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("getCannedResponses")), 0);
    QVERIFY(!pebble.cannedResponsesReady());
    QVERIFY(pebble.cannedResponses().isEmpty());
    QVERIFY(pebble.getCannedResponses(QStringList() << QStringLiteral("sms")).isEmpty());

    timer.restart();
    pebble.refreshCannedResponses();
    const qint64 refreshElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    QVERIFY(!pebble.cannedResponsesReady());

    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(responses));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), responses);
    QCOMPARE(pebble.getCannedResponses(
                 QStringList() << QStringLiteral("mail") << QStringLiteral("missing")),
             cannedResponseMap(QStringLiteral("mail"),
                               QStringList() << QStringLiteral("later")));
    QCOMPARE(pebble.getCannedResponses(QStringList()), responses);
    QCOMPARE(watch.receivedCount(QStringLiteral("getCannedResponses")), 0);
    QVERIFY2(constructionElapsed < 1000,
             "Pebble construction issued a blocking canned-response read");
    QVERIFY2(refreshElapsed < 1000,
             "Canned-response refresh waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestCannedResponsesReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-newest"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("old"));
    const QVariantMap newest = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("new"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("kept"));
    watch.setCannedResponsesValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedResponses();
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), initial);

    watch.defer(QStringLiteral("cannedResponses"));
    pebble.refreshCannedResponses();
    pebble.refreshCannedResponses();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 2);

    watch.replyPending(QStringLiteral("cannedResponses"), 1,
                       DelayedPebble::cannedResponsesArguments(newest));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), newest);

    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(initial));
    QTest::qWait(50);
    QVERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), newest);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerCannedResponsesReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    const QVariantMap oldResponses = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("old-owner"));
    firstWatch.setCannedResponsesValue(oldResponses);
    firstWatch.defer(QStringLiteral("cannedResponses"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedResponses();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("cannedResponses")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus,
        QStringLiteral("pebble-async-canned-responses-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    const QVariantMap newResponses = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("new-owner"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("current"));
    secondWatch.setCannedResponsesValue(newResponses);
    secondWatch.defer(QStringLiteral("cannedResponses"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("cannedResponses")), 1);
    QVERIFY(!pebble.cannedResponsesReady());
    QVERIFY(pebble.cannedResponses().isEmpty());
    secondWatch.replyNext(QStringLiteral("cannedResponses"),
                          DelayedPebble::cannedResponsesArguments(newResponses));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), newResponses);

    firstWatch.replyNext(QStringLiteral("cannedResponses"),
                         DelayedPebble::cannedResponsesArguments(oldResponses));
    QTest::qWait(50);
    QVERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), newResponses);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::cannedResponsesWritesMergeClearAndReadBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("old"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    const QVariantMap updated = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("new"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    const QVariantMap cleared = cannedResponseMap(
        QStringLiteral("sms"), QStringList(),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    watch.setCannedResponsesValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedResponses();
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), initial);

    watch.defer(QStringLiteral("setCannedResponses"));
    watch.defer(QStringLiteral("cannedResponses"));
    QElapsedTimer timer;
    timer.start();
    pebble.setCannedResponses(cannedResponseMap(
                                  QStringLiteral("sms"),
                                  QStringList() << QStringLiteral("new")));
    const qint64 elapsed = timer.elapsed();
    QCOMPARE(pebble.cannedResponses(), updated);
    QVERIFY(!pebble.cannedResponsesReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setCannedResponses")), 1);
    QCOMPARE(decodedCannedResponseMap(watch.cannedResponsesWrite()),
             cannedResponseMap(QStringLiteral("sms"), QStringList() << QStringLiteral("new")));

    watch.setCannedResponsesValue(updated);
    watch.replyNext(QStringLiteral("setCannedResponses"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(updated));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), updated);

    pebble.setCannedResponses(cannedResponseMap(QStringLiteral("sms"), QStringList()));
    QCOMPARE(pebble.cannedResponses(), cleared);
    QVERIFY(pebble.cannedResponses().contains(QStringLiteral("sms")));
    QVERIFY(pebble.cannedResponses().value(QStringLiteral("sms")).toStringList().isEmpty());
    QVERIFY(!pebble.cannedResponsesReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setCannedResponses")), 1);
    QCOMPARE(decodedCannedResponseMap(watch.cannedResponsesWrite(1)),
             cannedResponseMap(QStringLiteral("sms"), QStringList()));

    watch.setCannedResponsesValue(cleared);
    watch.replyNext(QStringLiteral("setCannedResponses"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(cleared));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), cleared);
    QVERIFY2(elapsed < 1000, "Canned-response setter waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedCannedResponsesWriteRollsBackAndReadsBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-write-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("old"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    const QVariantMap optimistic = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("new"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    watch.setCannedResponsesValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedResponses();
    QTRY_VERIFY(pebble.cannedResponsesReady());

    watch.defer(QStringLiteral("setCannedResponses"));
    watch.defer(QStringLiteral("cannedResponses"));
    pebble.setCannedResponses(cannedResponseMap(
                                  QStringLiteral("sms"),
                                  QStringList() << QStringLiteral("new")));
    QCOMPARE(pebble.cannedResponses(), optimistic);
    QVERIFY(!pebble.cannedResponsesReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setCannedResponses")), 1);

    watch.replyPendingError(QStringLiteral("setCannedResponses"), 0);
    QTRY_COMPARE(pebble.cannedResponses(), initial);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(initial));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), initial);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::staleCannedResponsesWriteErrorIsIgnored()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-stale-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("old"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    const QVariantMap second = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("second"),
        QStringLiteral("mail"), QStringList() << QStringLiteral("keep"));
    watch.setCannedResponsesValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedResponses();
    QTRY_VERIFY(pebble.cannedResponsesReady());

    watch.defer(QStringLiteral("setCannedResponses"));
    watch.defer(QStringLiteral("cannedResponses"));
    pebble.setCannedResponses(cannedResponseMap(
                                  QStringLiteral("sms"),
                                  QStringList() << QStringLiteral("first")));
    pebble.setCannedResponses(cannedResponseMap(
                                  QStringLiteral("sms"),
                                  QStringList() << QStringLiteral("second")));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setCannedResponses")), 2);
    QCOMPARE(pebble.cannedResponses(), second);

    watch.replyPendingError(QStringLiteral("setCannedResponses"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.cannedResponses(), second);
    QCOMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("setCannedResponses")), 1);

    watch.setCannedResponsesValue(second);
    watch.replyNext(QStringLiteral("setCannedResponses"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(second));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), second);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedCannedResponsesReadFallsBackAndRemainsWritable()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-responses-fallback"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap responses = cannedResponseMap(
        QStringLiteral("sms"), QStringList() << QStringLiteral("same"));
    watch.setCannedResponsesValue(responses);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedResponses();
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), responses);

    watch.defer(QStringLiteral("cannedResponses"));
    pebble.refreshCannedResponses();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    QVERIFY(!pebble.cannedResponsesReady());
    watch.replyPendingError(QStringLiteral("cannedResponses"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    QVERIFY(!pebble.cannedResponsesReady());
    watch.replyPendingError(QStringLiteral("cannedResponses"), 0);
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), responses);

    watch.defer(QStringLiteral("setCannedResponses"));
    pebble.setCannedResponses(responses);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setCannedResponses")), 1);
    QCOMPARE(decodedCannedResponseMap(watch.cannedResponsesWrite()), responses);
    watch.replyNext(QStringLiteral("setCannedResponses"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("cannedResponses")), 1);
    watch.replyNext(QStringLiteral("cannedResponses"),
                    DelayedPebble::cannedResponsesArguments(responses));
    QTRY_VERIFY(pebble.cannedResponsesReady());
    QCOMPARE(pebble.cannedResponses(), responses);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::cannedContactsLoadLazilyAndUseCachedGetter()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap contacts = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:111"),
        QStringLiteral("Bob"), QStringList() << QStringLiteral("tel:222"));
    watch.setFavoriteContactsValue(contacts);
    watch.defer(QStringLiteral("getFavoriteContacts"));
    registerWatch(&watch, connection);
    registerService(connection);

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 constructionElapsed = timer.elapsed();

    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("getFavoriteContacts")), 0);
    QVERIFY(!pebble.cannedContactsReady());
    QVERIFY(pebble.cannedContacts().isEmpty());
    QVERIFY(pebble.getCannedContacts(QStringList() << QStringLiteral("Alice")).isEmpty());

    timer.restart();
    pebble.refreshCannedContacts();
    const qint64 refreshElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    QVERIFY(!pebble.cannedContactsReady());

    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(contacts));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), contacts);
    QCOMPARE(pebble.getCannedContacts(
                 QStringList() << QStringLiteral("Bob") << QStringLiteral("Missing")),
             cannedResponseMap(QStringLiteral("Bob"), QStringList() << QStringLiteral("tel:222")));
    QCOMPARE(pebble.getCannedContacts(QStringList()), contacts);
    QCOMPARE(watch.receivedCount(QStringLiteral("getFavoriteContacts")), 1);
    QVERIFY2(constructionElapsed < 1000,
             "Pebble construction issued a blocking favorite-contact read");
    QVERIFY2(refreshElapsed < 1000,
             "Favorite-contact refresh waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestCannedContactsReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-newest"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:old"));
    const QVariantMap newest = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:new"),
        QStringLiteral("Bob"), QStringList() << QStringLiteral("tel:kept"));
    watch.setFavoriteContactsValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedContacts();
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), initial);

    watch.defer(QStringLiteral("getFavoriteContacts"));
    pebble.refreshCannedContacts();
    pebble.refreshCannedContacts();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 2);

    watch.replyPending(QStringLiteral("getFavoriteContacts"), 1,
                       DelayedPebble::favoriteContactsArguments(newest));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), newest);

    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(initial));
    QTest::qWait(50);
    QVERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), newest);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerCannedContactsReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    const QVariantMap oldContacts = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:old-owner"));
    firstWatch.setFavoriteContactsValue(oldContacts);
    firstWatch.defer(QStringLiteral("getFavoriteContacts"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedContacts();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus,
        QStringLiteral("pebble-async-canned-contacts-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    const QVariantMap newContacts = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:new-owner"),
        QStringLiteral("Bob"), QStringList() << QStringLiteral("tel:current"));
    secondWatch.setFavoriteContactsValue(newContacts);
    secondWatch.defer(QStringLiteral("getFavoriteContacts"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    QVERIFY(!pebble.cannedContactsReady());
    QVERIFY(pebble.cannedContacts().isEmpty());
    secondWatch.replyNext(QStringLiteral("getFavoriteContacts"),
                          DelayedPebble::favoriteContactsArguments(newContacts));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), newContacts);

    firstWatch.replyNext(QStringLiteral("getFavoriteContacts"),
                         DelayedPebble::favoriteContactsArguments(oldContacts));
    QTest::qWait(50);
    QVERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), newContacts);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::cannedContactsWritesReplaceClearAndReadBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:old"),
        QStringLiteral("Bob"), QStringList() << QStringLiteral("tel:keep"));
    const QVariantMap replacement = cannedResponseMap(
        QStringLiteral("Alice"), QStringList(),
        QStringLiteral("Carol"), QStringList() << QStringLiteral("tel:new"));
    watch.setFavoriteContactsValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedContacts();
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), initial);

    watch.defer(QStringLiteral("setFavoriteContacts"));
    watch.defer(QStringLiteral("getFavoriteContacts"));
    QElapsedTimer timer;
    timer.start();
    pebble.setCannedContacts(replacement);
    const qint64 elapsed = timer.elapsed();
    QCOMPARE(pebble.cannedContacts(), replacement);
    QVERIFY(!pebble.cannedContactsReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setFavoriteContacts")), 1);
    QCOMPARE(decodedCannedResponseMap(watch.favoriteContactsWrite()), replacement);
    QVERIFY(!pebble.cannedContacts().contains(QStringLiteral("Bob")));
    QVERIFY(pebble.cannedContacts().contains(QStringLiteral("Alice")));
    QVERIFY(pebble.cannedContacts().value(QStringLiteral("Alice")).toStringList().isEmpty());

    watch.setFavoriteContactsValue(replacement);
    watch.replyNext(QStringLiteral("setFavoriteContacts"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(replacement));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), replacement);

    pebble.setCannedContacts(QVariantMap());
    QVERIFY(pebble.cannedContacts().isEmpty());
    QVERIFY(!pebble.cannedContactsReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setFavoriteContacts")), 1);
    QCOMPARE(decodedCannedResponseMap(watch.favoriteContactsWrite(1)), QVariantMap());

    watch.setFavoriteContactsValue(QVariantMap());
    watch.replyNext(QStringLiteral("setFavoriteContacts"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(QVariantMap()));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QVERIFY(pebble.cannedContacts().isEmpty());
    QVERIFY2(elapsed < 1000, "Favorite-contact setter waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedCannedContactsWriteRollsBackAndReadsBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-write-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:old"));
    const QVariantMap optimistic = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:new"));
    watch.setFavoriteContactsValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedContacts();
    QTRY_VERIFY(pebble.cannedContactsReady());

    watch.defer(QStringLiteral("setFavoriteContacts"));
    watch.defer(QStringLiteral("getFavoriteContacts"));
    pebble.setCannedContacts(optimistic);
    QCOMPARE(pebble.cannedContacts(), optimistic);
    QVERIFY(!pebble.cannedContactsReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setFavoriteContacts")), 1);

    watch.replyPendingError(QStringLiteral("setFavoriteContacts"), 0);
    QTRY_COMPARE(pebble.cannedContacts(), initial);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(initial));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), initial);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::staleCannedContactsWriteErrorIsIgnored()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-stale-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap initial = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:old"));
    const QVariantMap second = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:second"));
    watch.setFavoriteContactsValue(initial);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedContacts();
    QTRY_VERIFY(pebble.cannedContactsReady());

    watch.defer(QStringLiteral("setFavoriteContacts"));
    watch.defer(QStringLiteral("getFavoriteContacts"));
    pebble.setCannedContacts(cannedResponseMap(
                                QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:first")));
    pebble.setCannedContacts(second);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setFavoriteContacts")), 2);
    QCOMPARE(pebble.cannedContacts(), second);

    watch.replyPendingError(QStringLiteral("setFavoriteContacts"), 0);
    QTest::qWait(50);
    QCOMPARE(pebble.cannedContacts(), second);
    QCOMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("setFavoriteContacts")), 1);

    watch.setFavoriteContactsValue(second);
    watch.replyNext(QStringLiteral("setFavoriteContacts"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(second));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), second);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedCannedContactsReadFallsBackAndRemainsWritable()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-canned-contacts-fallback"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantMap contacts = cannedResponseMap(
        QStringLiteral("Alice"), QStringList() << QStringLiteral("tel:same"));
    watch.setFavoriteContactsValue(contacts);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshCannedContacts();
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), contacts);

    watch.defer(QStringLiteral("getFavoriteContacts"));
    pebble.refreshCannedContacts();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    QVERIFY(!pebble.cannedContactsReady());
    watch.replyPendingError(QStringLiteral("getFavoriteContacts"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    QVERIFY(!pebble.cannedContactsReady());
    watch.replyPendingError(QStringLiteral("getFavoriteContacts"), 0);
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), contacts);

    watch.defer(QStringLiteral("setFavoriteContacts"));
    pebble.setCannedContacts(contacts);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setFavoriteContacts")), 1);
    QCOMPARE(decodedCannedResponseMap(watch.favoriteContactsWrite()), contacts);
    watch.replyNext(QStringLiteral("setFavoriteContacts"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getFavoriteContacts")), 1);
    watch.replyNext(QStringLiteral("getFavoriteContacts"),
                    DelayedPebble::favoriteContactsArguments(contacts));
    QTRY_VERIFY(pebble.cannedContactsReady());
    QCOMPARE(pebble.cannedContacts(), contacts);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::weatherSettingsLoadLazilyAndAtomically()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-weather-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantList locations = weatherLocations(QStringLiteral("Paris"),
                                                    QStringLiteral("48.8566"),
                                                    QStringLiteral("2.3522"));
    watch.setWeatherLocationsValue(locations);
    watch.setWeatherUnitsValue(QStringLiteral("e"));
    watch.setWeatherLanguageValue(QStringLiteral("fr"));
    watch.setWeatherAltKeyValue(QStringLiteral("weather-key"));
    deferWeatherSettings(&watch);
    registerWatch(&watch, connection);
    registerService(connection);

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 constructionElapsed = timer.elapsed();

    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("WeatherLocations")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("WeatherUnits")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("WeatherLanguage")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("WeatherAltKey")), 0);
    QVERIFY(!pebble.weatherSettingsReady());
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("m"));

    timer.restart();
    pebble.refreshWeatherSettings();
    const qint64 refreshElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherLocations")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherLanguage")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherAltKey")), 1);

    watch.replyNext(QStringLiteral("WeatherUnits"), QVariantList() << QStringLiteral("e"));
    QTRY_COMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QVERIFY(!pebble.weatherSettingsReady());
    watch.replyNext(QStringLiteral("WeatherLanguage"), QVariantList() << QStringLiteral("fr"));
    QTRY_COMPARE(pebble.weatherLanguage(), QStringLiteral("fr"));
    QVERIFY(!pebble.weatherSettingsReady());
    watch.replyNext(QStringLiteral("WeatherAltKey"), QVariantList() << QStringLiteral("weather-key"));
    QTRY_COMPARE(pebble.weatherAltKey(), QStringLiteral("weather-key"));
    QVERIFY(!pebble.weatherSettingsReady());
    watch.replyNext(QStringLiteral("WeatherLocations"),
                    DelayedPebble::weatherLocationsArguments(locations));
    QTRY_VERIFY(pebble.weatherSettingsReady());
    QCOMPARE(pebble.weatherLocations(), locations);

    qDebug() << "weather construction elapsed" << constructionElapsed
             << "weather refresh elapsed" << refreshElapsed;
    QVERIFY2(constructionElapsed < 1000, "Pebble construction issued blocking weather reads");
    QVERIFY2(refreshElapsed < 1000, "Weather refresh waited for D-Bus replies");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestWeatherSettingsReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-weather-newest"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));

    const QVariantList initialLocations = weatherLocations(QStringLiteral("Berlin"),
                                                           QStringLiteral("52.5200"),
                                                           QStringLiteral("13.4050"));
    watch.setWeatherLocationsValue(initialLocations);
    watch.setWeatherUnitsValue(QStringLiteral("m"));
    watch.setWeatherLanguageValue(QStringLiteral("en"));
    watch.setWeatherAltKeyValue(QStringLiteral("initial-key"));
    pebble.refreshWeatherSettings();
    QTRY_VERIFY(pebble.weatherSettingsReady());

    const QVariantList oldLocations = weatherLocations(QStringLiteral("Oldtown"),
                                                        QStringLiteral("1.0000"),
                                                        QStringLiteral("2.0000"));
    const QVariantList newestLocations = weatherLocations(QStringLiteral("Helsinki"),
                                                           QStringLiteral("60.1699"),
                                                           QStringLiteral("24.9384"));
    deferWeatherSettings(&watch);
    pebble.refreshWeatherSettings();
    pebble.refreshWeatherSettings();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherLocations")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherUnits")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherLanguage")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherAltKey")), 2);

    watch.replyPending(QStringLiteral("WeatherLocations"), 1,
                       DelayedPebble::weatherLocationsArguments(newestLocations));
    watch.replyPending(QStringLiteral("WeatherUnits"), 1, QVariantList() << QStringLiteral("e"));
    watch.replyPending(QStringLiteral("WeatherLanguage"), 1, QVariantList() << QStringLiteral("fi"));
    watch.replyPending(QStringLiteral("WeatherAltKey"), 1,
                       QVariantList() << QStringLiteral("newest-key"));
    QTRY_VERIFY(pebble.weatherSettingsReady());
    QCOMPARE(pebble.weatherLocations(), newestLocations);
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QCOMPARE(pebble.weatherLanguage(), QStringLiteral("fi"));
    QCOMPARE(pebble.weatherAltKey(), QStringLiteral("newest-key"));

    watch.replyNext(QStringLiteral("WeatherLocations"),
                    DelayedPebble::weatherLocationsArguments(oldLocations));
    watch.replyNext(QStringLiteral("WeatherUnits"), QVariantList() << QStringLiteral("m"));
    watch.replyNext(QStringLiteral("WeatherLanguage"), QVariantList() << QStringLiteral("en"));
    watch.replyNext(QStringLiteral("WeatherAltKey"), QVariantList() << QStringLiteral("old-key"));
    QTest::qWait(50);
    QCOMPARE(pebble.weatherLocations(), newestLocations);
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QCOMPARE(pebble.weatherLanguage(), QStringLiteral("fi"));
    QCOMPARE(pebble.weatherAltKey(), QStringLiteral("newest-key"));

    const QVariantList signalLocations = weatherLocations(QStringLiteral("Tampere"),
                                                           QStringLiteral("61.4978"),
                                                           QStringLiteral("23.7610"));
    watch.setWeatherLocationsValue(signalLocations);
    watch.emitPebbleSignal(QStringLiteral("WeatherLocationsChanged"),
                           DelayedPebble::weatherLocationsArguments(signalLocations));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherLocations")), 1);
    watch.replyNext(QStringLiteral("WeatherLocations"),
                    DelayedPebble::weatherLocationsArguments(signalLocations));
    QTRY_COMPARE(pebble.weatherLocations(), signalLocations);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerWeatherSettingsReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-weather-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    const QVariantList oldLocations = weatherLocations(QStringLiteral("Oldtown"),
                                                        QStringLiteral("1.0000"),
                                                        QStringLiteral("2.0000"));
    firstWatch.setWeatherLocationsValue(oldLocations);
    firstWatch.setWeatherUnitsValue(QStringLiteral("m"));
    firstWatch.setWeatherLanguageValue(QStringLiteral("en"));
    firstWatch.setWeatherAltKeyValue(QStringLiteral("old-key"));
    deferWeatherSettings(&firstWatch);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshWeatherSettings();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("WeatherLocations")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("WeatherUnits")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("WeatherLanguage")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("WeatherAltKey")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-weather-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    const QVariantList newestLocations = weatherLocations(QStringLiteral("Helsinki"),
                                                           QStringLiteral("60.1699"),
                                                           QStringLiteral("24.9384"));
    secondWatch.setWeatherLocationsValue(newestLocations);
    secondWatch.setWeatherUnitsValue(QStringLiteral("e"));
    secondWatch.setWeatherLanguageValue(QStringLiteral("fi"));
    secondWatch.setWeatherAltKeyValue(QStringLiteral("newest-key"));
    deferWeatherSettings(&secondWatch);
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("WeatherLocations")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("WeatherUnits")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("WeatherLanguage")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("WeatherAltKey")), 1);
    QVERIFY(!pebble.weatherSettingsReady());
    replyWeatherSettings(&secondWatch, newestLocations, QStringLiteral("e"),
                         QStringLiteral("fi"), QStringLiteral("newest-key"));
    QTRY_VERIFY(pebble.weatherSettingsReady());
    QCOMPARE(pebble.weatherLocations(), newestLocations);
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QCOMPARE(pebble.weatherLanguage(), QStringLiteral("fi"));
    QCOMPARE(pebble.weatherAltKey(), QStringLiteral("newest-key"));

    replyWeatherSettings(&firstWatch, oldLocations, QStringLiteral("m"),
                         QStringLiteral("en"), QStringLiteral("old-key"));
    QTest::qWait(50);
    QVERIFY(pebble.weatherSettingsReady());
    QCOMPARE(pebble.weatherLocations(), newestLocations);
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QCOMPARE(pebble.weatherLanguage(), QStringLiteral("fi"));
    QCOMPARE(pebble.weatherAltKey(), QStringLiteral("newest-key"));
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::weatherSettersDoNotWaitForReplies()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-weather-setters"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    const QVariantList initialLocations = weatherLocations(QStringLiteral("Paris"),
                                                           QStringLiteral("48.8566"),
                                                           QStringLiteral("2.3522"));
    watch.setWeatherLocationsValue(initialLocations);
    watch.setWeatherUnitsValue(QStringLiteral("m"));
    watch.setWeatherLanguageValue(QStringLiteral("en"));
    watch.setWeatherAltKeyValue(QStringLiteral("initial-key"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshWeatherSettings();
    QTRY_VERIFY(pebble.weatherSettingsReady());

    const QVariantList expectedLocations = weatherLocations(QStringLiteral("Lyon"),
                                                            QStringLiteral("45.7640"),
                                                            QStringLiteral("4.8357"));
    watch.defer(QStringLiteral("SetWeatherLocations"));
    watch.defer(QStringLiteral("setWeatherUnits"));
    watch.defer(QStringLiteral("setWeatherLanguage"));
    watch.defer(QStringLiteral("setWeatherAltKey"));
    QElapsedTimer timer;
    timer.start();
    pebble.setWeatherLocations(expectedLocations);
    pebble.setWeatherUnits(QStringLiteral("e"));
    pebble.setWeatherLanguage(QStringLiteral("fr"));
    pebble.setWeatherAltKey(QStringLiteral("new-key"));
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(pebble.weatherLocations(), expectedLocations);
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QCOMPARE(pebble.weatherLanguage(), QStringLiteral("fr"));
    QCOMPARE(pebble.weatherAltKey(), QStringLiteral("new-key"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetWeatherLocations")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setWeatherUnits")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setWeatherLanguage")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setWeatherAltKey")), 1);
    QVERIFY(!watch.arguments(QStringLiteral("SetWeatherLocations")).isEmpty());
    QCOMPARE(watch.arguments(QStringLiteral("setWeatherUnits")).first().toString(),
             QStringLiteral("e"));
    QCOMPARE(watch.arguments(QStringLiteral("setWeatherLanguage")).first().toString(),
             QStringLiteral("fr"));
    QCOMPARE(watch.arguments(QStringLiteral("setWeatherAltKey")).first().toString(),
             QStringLiteral("new-key"));

    watch.setWeatherLocationsValue(expectedLocations);
    watch.setWeatherUnitsValue(QStringLiteral("e"));
    watch.setWeatherLanguageValue(QStringLiteral("fr"));
    watch.setWeatherAltKeyValue(QStringLiteral("new-key"));
    watch.replyNext(QStringLiteral("SetWeatherLocations"), QVariantList());
    watch.replyNext(QStringLiteral("setWeatherUnits"), QVariantList());
    watch.replyNext(QStringLiteral("setWeatherLanguage"), QVariantList());
    watch.replyNext(QStringLiteral("setWeatherAltKey"), QVariantList());
    QTest::qWait(50);
    QCOMPARE(pebble.weatherLocations(), expectedLocations);
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QCOMPARE(pebble.weatherLanguage(), QStringLiteral("fr"));
    QCOMPARE(pebble.weatherAltKey(), QStringLiteral("new-key"));
    QVERIFY2(elapsed < 1000, "Weather setter waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedWeatherSetterRefreshesAndRollsBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-weather-write-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setWeatherUnitsValue(QStringLiteral("m"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshWeatherSettings();
    QTRY_VERIFY(pebble.weatherSettingsReady());
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("m"));

    const int unitReadsBefore = watch.receivedCount(QStringLiteral("WeatherUnits"));
    watch.defer(QStringLiteral("setWeatherUnits"));
    watch.defer(QStringLiteral("WeatherUnits"));
    QElapsedTimer timer;
    timer.start();
    pebble.setWeatherUnits(QStringLiteral("e"));
    const qint64 elapsed = timer.elapsed();
    QCOMPARE(pebble.weatherUnits(), QStringLiteral("e"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setWeatherUnits")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("setWeatherUnits")).first().toString(),
             QStringLiteral("e"));

    watch.setWeatherUnitsValue(QStringLiteral("m"));
    watch.replyPendingError(QStringLiteral("setWeatherUnits"), 0);
    QTRY_COMPARE(pebble.weatherUnits(), QStringLiteral("m"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("WeatherUnits")), 1);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("WeatherUnits")), unitReadsBefore + 1);
    watch.replyNext(QStringLiteral("WeatherUnits"), QVariantList() << QStringLiteral("m"));
    QTRY_COMPARE(pebble.weatherUnits(), QStringLiteral("m"));
    QVERIFY2(elapsed < 1000, "Weather setter waited for a failed D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::developerSettingsLoadLazilyAndAtomically()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-lazy"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setDevConnectionEnabledValue(true);
    watch.setDevConnectionStateValue(true);
    watch.setLogLevelValue(0);
    deferDeveloperSettings(&watch);
    registerWatch(&watch, connection);
    registerService(connection);

    QElapsedTimer timer;
    timer.start();
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    const qint64 constructionElapsed = timer.elapsed();

    QTest::qWait(50);
    QCOMPARE(watch.receivedCount(QStringLiteral("DevConnectionEnabled")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("DevConnectionState")), 0);
    QCOMPARE(watch.receivedCount(QStringLiteral("getLogLevel")), 0);
    QVERIFY(!pebble.developerSettingsReady());
    QVERIFY(!pebble.devConnEnabled());
    QVERIFY(!pebble.devConnServerRunning());
    QCOMPARE(pebble.getLogLevel(), 1);

    timer.restart();
    pebble.refreshDeveloperSettings();
    const qint64 refreshElapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionState")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getLogLevel")), 1);

    watch.replyNext(QStringLiteral("DevConnectionEnabled"), QVariantList() << true);
    QTRY_VERIFY(pebble.devConnEnabled());
    QVERIFY(!pebble.developerSettingsReady());
    watch.replyNext(QStringLiteral("DevConnectionState"), QVariantList() << true);
    QTRY_VERIFY(pebble.devConnServerRunning());
    QVERIFY(!pebble.developerSettingsReady());
    watch.replyNext(QStringLiteral("getLogLevel"), QVariantList() << 0);
    QTRY_VERIFY(pebble.developerSettingsReady());
    QCOMPARE(pebble.getLogLevel(), 0);

    qDebug() << "developer construction elapsed" << constructionElapsed
             << "developer refresh elapsed" << refreshElapsed;
    QVERIFY2(constructionElapsed < 1000,
             "Pebble construction issued blocking developer settings reads");
    QVERIFY2(refreshElapsed < 1000,
             "Developer settings refresh waited for D-Bus replies");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newestDeveloperSettingsReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-newest"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setDevConnectionEnabledValue(false);
    watch.setDevConnectionStateValue(false);
    watch.setLogLevelValue(1);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshDeveloperSettings();
    QTRY_VERIFY(pebble.developerSettingsReady());

    deferDeveloperSettings(&watch);
    pebble.refreshDeveloperSettings();
    pebble.refreshDeveloperSettings();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionEnabled")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionState")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getLogLevel")), 2);

    watch.replyPending(QStringLiteral("DevConnectionEnabled"), 1, QVariantList() << true);
    watch.replyPending(QStringLiteral("DevConnectionState"), 1, QVariantList() << true);
    watch.replyPending(QStringLiteral("getLogLevel"), 1, QVariantList() << 0);
    QTRY_VERIFY(pebble.developerSettingsReady());
    QVERIFY(pebble.devConnEnabled());
    QVERIFY(pebble.devConnServerRunning());
    QCOMPARE(pebble.getLogLevel(), 0);

    watch.replyNext(QStringLiteral("DevConnectionEnabled"), QVariantList() << false);
    watch.replyNext(QStringLiteral("DevConnectionState"), QVariantList() << false);
    watch.replyNext(QStringLiteral("getLogLevel"), QVariantList() << 1);
    QTest::qWait(50);
    QVERIFY(pebble.developerSettingsReady());
    QVERIFY(pebble.devConnEnabled());
    QVERIFY(pebble.devConnServerRunning());
    QCOMPARE(pebble.getLogLevel(), 0);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::developerConnectionSignalRefreshesCurrentValues()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-signal"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setDevConnectionEnabledValue(false);
    watch.setDevConnectionStateValue(false);
    watch.setLogLevelValue(1);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshDeveloperSettings();
    QTRY_VERIFY(pebble.developerSettingsReady());

    deferDeveloperSettings(&watch);
    pebble.refreshDeveloperSettings();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionState")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getLogLevel")), 1);

    watch.setDevConnectionEnabledValue(true);
    watch.setDevConnectionStateValue(true);
    watch.emitPebbleSignal(QStringLiteral("DevConnectionChanged"), QVariantList() << true);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionEnabled")), 2);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionState")), 2);
    QCOMPARE(watch.pendingCount(QStringLiteral("getLogLevel")), 1);
    QVERIFY(!pebble.devConnEnabled());
    QVERIFY(!pebble.devConnServerRunning());

    watch.replyPending(QStringLiteral("DevConnectionEnabled"), 1, QVariantList() << true);
    watch.replyPending(QStringLiteral("DevConnectionState"), 1, QVariantList() << true);
    QTRY_VERIFY(pebble.devConnEnabled());
    QTRY_VERIFY(pebble.devConnServerRunning());
    watch.replyNext(QStringLiteral("getLogLevel"), QVariantList() << 0);
    QTRY_VERIFY(pebble.developerSettingsReady());
    QCOMPARE(pebble.getLogLevel(), 0);

    watch.replyNext(QStringLiteral("DevConnectionEnabled"), QVariantList() << false);
    watch.replyNext(QStringLiteral("DevConnectionState"), QVariantList() << false);
    QTest::qWait(50);
    QVERIFY(pebble.devConnEnabled());
    QVERIFY(pebble.devConnServerRunning());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerDeveloperSettingsReplyIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.setDevConnectionEnabledValue(false);
    firstWatch.setDevConnectionStateValue(false);
    firstWatch.setLogLevelValue(1);
    deferDeveloperSettings(&firstWatch);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshDeveloperSettings();
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("DevConnectionEnabled")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("DevConnectionState")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("getLogLevel")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.setDevConnectionEnabledValue(true);
    secondWatch.setDevConnectionStateValue(true);
    secondWatch.setLogLevelValue(0);
    deferDeveloperSettings(&secondWatch);
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("DevConnectionEnabled")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("DevConnectionState")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("getLogLevel")), 1);
    QVERIFY(!pebble.developerSettingsReady());
    replyDeveloperSettings(&secondWatch, true, true, 0);
    QTRY_VERIFY(pebble.developerSettingsReady());
    QVERIFY(pebble.devConnEnabled());
    QVERIFY(pebble.devConnServerRunning());
    QCOMPARE(pebble.getLogLevel(), 0);

    replyDeveloperSettings(&firstWatch, false, false, 1);
    QTest::qWait(50);
    QVERIFY(pebble.developerSettingsReady());
    QVERIFY(pebble.devConnEnabled());
    QVERIFY(pebble.devConnServerRunning());
    QCOMPARE(pebble.getLogLevel(), 0);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::developerSettersDoNotWaitForReplies()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-setters"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setDevConnectionEnabledValue(false);
    watch.setDevConnectionStateValue(false);
    watch.setLogLevelValue(1);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshDeveloperSettings();
    QTRY_VERIFY(pebble.developerSettingsReady());

    const int enabledReadsBefore = watch.receivedCount(QStringLiteral("DevConnectionEnabled"));
    const int stateReadsBefore = watch.receivedCount(QStringLiteral("DevConnectionState"));
    const int logReadsBefore = watch.receivedCount(QStringLiteral("getLogLevel"));
    watch.defer(QStringLiteral("SetDevConnEnabled"));
    watch.defer(QStringLiteral("setLogLevel"));
    QElapsedTimer timer;
    timer.start();
    pebble.setDevConnEnabled(true);
    pebble.setLogLevel(0);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(pebble.devConnEnabled());
    QVERIFY(!pebble.devConnServerRunning());
    QCOMPARE(pebble.getLogLevel(), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetDevConnEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setLogLevel")), 1);
    QVERIFY(watch.arguments(QStringLiteral("SetDevConnEnabled")).first().toBool());
    QCOMPARE(watch.arguments(QStringLiteral("setLogLevel")).first().toInt(), 0);

    watch.setDevConnectionEnabledValue(true);
    watch.setDevConnectionStateValue(true);
    watch.setLogLevelValue(0);
    watch.replyNext(QStringLiteral("SetDevConnEnabled"), QVariantList());
    watch.replyNext(QStringLiteral("setLogLevel"), QVariantList());
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("DevConnectionEnabled")),
                 enabledReadsBefore + 1);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("DevConnectionState")),
                 stateReadsBefore + 1);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("getLogLevel")), logReadsBefore + 1);
    QTRY_VERIFY(pebble.devConnEnabled());
    QTRY_VERIFY(pebble.devConnServerRunning());
    QTRY_COMPARE(pebble.getLogLevel(), 0);
    QVERIFY2(elapsed < 1000, "Developer settings setter waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::failedDeveloperSettersRefreshAndRollBack()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-developer-write-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setDevConnectionEnabledValue(false);
    watch.setDevConnectionStateValue(false);
    watch.setLogLevelValue(1);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshDeveloperSettings();
    QTRY_VERIFY(pebble.developerSettingsReady());

    const int enabledReadsBefore = watch.receivedCount(QStringLiteral("DevConnectionEnabled"));
    const int stateReadsBefore = watch.receivedCount(QStringLiteral("DevConnectionState"));
    watch.defer(QStringLiteral("SetDevConnEnabled"));
    watch.defer(QStringLiteral("DevConnectionEnabled"));
    watch.defer(QStringLiteral("DevConnectionState"));
    pebble.setDevConnEnabled(true);
    QVERIFY(pebble.devConnEnabled());
    QVERIFY(!pebble.devConnServerRunning());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetDevConnEnabled")), 1);
    watch.setDevConnectionEnabledValue(false);
    watch.setDevConnectionStateValue(false);
    watch.replyPendingError(QStringLiteral("SetDevConnEnabled"), 0);
    QTRY_VERIFY(!pebble.devConnEnabled());
    QVERIFY(!pebble.devConnServerRunning());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionEnabled")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DevConnectionState")), 1);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("DevConnectionEnabled")),
                 enabledReadsBefore + 1);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("DevConnectionState")),
                 stateReadsBefore + 1);
    watch.replyNext(QStringLiteral("DevConnectionEnabled"), QVariantList() << false);
    watch.replyNext(QStringLiteral("DevConnectionState"), QVariantList() << false);
    QTRY_VERIFY(!pebble.devConnEnabled());
    QVERIFY(!pebble.devConnServerRunning());

    const int logReadsBefore = watch.receivedCount(QStringLiteral("getLogLevel"));
    watch.defer(QStringLiteral("setLogLevel"));
    watch.defer(QStringLiteral("getLogLevel"));
    pebble.setLogLevel(0);
    QCOMPARE(pebble.getLogLevel(), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setLogLevel")), 1);
    watch.setLogLevelValue(1);
    watch.replyPendingError(QStringLiteral("setLogLevel"), 0);
    QTRY_COMPARE(pebble.getLogLevel(), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("getLogLevel")), 1);
    QTRY_COMPARE(watch.receivedCount(QStringLiteral("getLogLevel")), logReadsBefore + 1);
    watch.replyNext(QStringLiteral("getLogLevel"), QVariantList() << 1);
    QTRY_COMPARE(pebble.getLogLevel(), 1);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::dumpLogsDoesNotWaitForReplyAndReportsError()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-dump-logs"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QSignalSpy logsDumpedSpy(&pebble,
                             QMetaMethod::fromSignal(&Pebble::logsDumped));
    watch.defer(QStringLiteral("DumpLogs"));

    QElapsedTimer timer;
    timer.start();
    pebble.dumpLogs(QStringLiteral("/tmp/pebble-test.log"));
    const qint64 elapsed = timer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DumpLogs")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("DumpLogs")).first().toString(),
             QStringLiteral("/tmp/pebble-test.log"));
    watch.replyNext(QStringLiteral("DumpLogs"), QVariantList());
    QTest::qWait(50);
    QCOMPARE(logsDumpedSpy.count(), 0);
    watch.emitPebbleSignal(QStringLiteral("LogsDumped"), QVariantList() << true);
    QTRY_COMPARE(logsDumpedSpy.count(), 1);
    QCOMPARE(logsDumpedSpy.takeFirst().at(0).toBool(), true);

    pebble.dumpLogs(QStringLiteral("/tmp/pebble-test.log"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("DumpLogs")), 1);
    watch.replyPendingError(QStringLiteral("DumpLogs"), 0);
    QTRY_COMPARE(logsDumpedSpy.count(), 1);
    QCOMPARE(logsDumpedSpy.takeFirst().at(0).toBool(), false);
    QVERIFY2(elapsed < 1000, "DumpLogs waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::dumpLogsOwnerChangeFailsOnceAndIgnoresOldCompletion()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-dump-logs-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QSignalSpy logsDumpedSpy(&pebble,
                             QMetaMethod::fromSignal(&Pebble::logsDumped));
    firstWatch.defer(QStringLiteral("DumpLogs"));
    pebble.dumpLogs(QStringLiteral("/tmp/old-owner.log"));
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("DumpLogs")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-dump-logs-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.defer(QStringLiteral("DumpLogs"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);

    QTRY_COMPARE(logsDumpedSpy.count(), 1);
    QCOMPARE(logsDumpedSpy.takeFirst().at(0).toBool(), false);
    QTest::qWait(50);
    QCOMPARE(logsDumpedSpy.count(), 0);

    pebble.dumpLogs(QStringLiteral("/tmp/current-owner.log"));
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("DumpLogs")), 1);
    firstWatch.replyNext(QStringLiteral("DumpLogs"), QVariantList());
    firstWatch.emitPebbleSignal(QStringLiteral("LogsDumped"), QVariantList() << true);
    QTest::qWait(50);
    QCOMPARE(logsDumpedSpy.count(), 0);
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("DumpLogs")), 1);

    secondWatch.replyNext(QStringLiteral("DumpLogs"), QVariantList());
    QTest::qWait(50);
    QCOMPARE(logsDumpedSpy.count(), 0);
    secondWatch.emitPebbleSignal(QStringLiteral("LogsDumped"), QVariantList() << true);
    QTRY_COMPARE(logsDumpedSpy.count(), 1);
    QCOMPARE(logsDumpedSpy.takeFirst().at(0).toBool(), true);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::timelineActionsDoNotWaitAndWindowReadbackIsCanonical()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-actions"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    watch.defer(QStringLiteral("resetTimeline"));
    watch.defer(QStringLiteral("setTimelineWindow"));
    watch.defer(QStringLiteral("timelineWindowStart"));
    watch.defer(QStringLiteral("timelineWindowFade"));
    watch.defer(QStringLiteral("timelineWindowEnd"));

    QElapsedTimer resetTimer;
    resetTimer.start();
    pebble.resetTimeline();
    const qint64 resetElapsed = resetTimer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("resetTimeline")), 1);

    QElapsedTimer windowTimer;
    windowTimer.start();
    pebble.setTimelineWindow(23, 7, 11);
    const qint64 windowElapsed = windowTimer.elapsed();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    const QVariantList arguments = watch.arguments(QStringLiteral("setTimelineWindow"));
    QCOMPARE(arguments.count(), 3);
    QCOMPARE(arguments.at(0).toInt(), -23);
    QCOMPARE(arguments.at(1).toInt(), -7);
    QCOMPARE(arguments.at(2).toInt(), 11);

    watch.replyNext(QStringLiteral("resetTimeline"), QVariantList());
    QTest::qWait(50);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 0);

    watch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -31);
    watch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -13);
    watch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 17);
    QTRY_COMPARE(pebble.timelineWindowStart(), 31);
    QTRY_COMPARE(pebble.timelineWindowFade(), 13);
    QTRY_COMPARE(pebble.timelineWindowEnd(), 17);
    QTRY_VERIFY(pebble.timelineWindowReady());
    QVERIFY2(resetElapsed < 1000, "resetTimeline waited for its D-Bus reply");
    QVERIFY2(windowElapsed < 1000, "setTimelineWindow waited for its D-Bus reply");
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::resetTimelineErrorDoesNotChangeTimelineWindow()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-reset-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    QSignalSpy timelineChangedSpy(&pebble,
                                  QMetaMethod::fromSignal(&Pebble::timelineWindowChanged));
    watch.defer(QStringLiteral("resetTimeline"));

    pebble.resetTimeline();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("resetTimeline")), 1);
    watch.replyPendingError(QStringLiteral("resetTimeline"), 0);
    QTest::qWait(50);
    QCOMPARE(timelineChangedSpy.count(), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 0);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::rejectedTimelineWindowWriteRollsBackToCanonicalReadback()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-write-error"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setTimelineWindowWireValues(-5, -6, 7);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 5);
    QCOMPARE(pebble.timelineWindowFade(), 6);
    QCOMPARE(pebble.timelineWindowEnd(), 7);
    watch.defer(QStringLiteral("setTimelineWindow"));
    watch.defer(QStringLiteral("timelineWindowStart"));
    watch.defer(QStringLiteral("timelineWindowFade"));
    watch.defer(QStringLiteral("timelineWindowEnd"));

    pebble.setTimelineWindow(20, 30, 40);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    QVERIFY(!pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 5);
    QCOMPARE(pebble.timelineWindowFade(), 6);
    QCOMPARE(pebble.timelineWindowEnd(), 7);
    watch.replyPendingError(QStringLiteral("setTimelineWindow"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -5);
    watch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -6);
    watch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 7);
    QTRY_VERIFY(pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 5);
    QCOMPARE(pebble.timelineWindowFade(), 6);
    QCOMPARE(pebble.timelineWindowEnd(), 7);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::newerTimelineWindowWriteBeatsOlderReplyAndReadback()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-write-order"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    watch.defer(QStringLiteral("setTimelineWindow"));
    watch.defer(QStringLiteral("timelineWindowStart"));
    watch.defer(QStringLiteral("timelineWindowFade"));
    watch.defer(QStringLiteral("timelineWindowEnd"));

    pebble.setTimelineWindow(2, 3, 4);
    pebble.setTimelineWindow(20, 30, 40);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("setTimelineWindow"), 0),
             QVariantList() << -2 << -3 << 4);
    watch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("setTimelineWindow"), 1),
             QVariantList() << -20 << -30 << 40);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 0);

    watch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -21);
    watch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -31);
    watch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 41);
    QTRY_COMPARE(pebble.timelineWindowStart(), 21);
    QTRY_COMPARE(pebble.timelineWindowFade(), 31);
    QTRY_COMPARE(pebble.timelineWindowEnd(), 41);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::quietTimeAsyncSaveFailureAndSignals()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-quiet-time"));
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("QuietTimeSettings"));
    watch.defer(QStringLiteral("SetQuietTimeSetting"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QCOMPARE(watch.receivedCount(QStringLiteral("QuietTimeSettings")), 0);
    pebble.refreshQuietTime();
    QVERIFY(pebble.quietTimeBusy());
    QVERIFY(!pebble.quietTimeReady());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("QuietTimeSettings")), 1);
    QVariantMap values;
    values.insert(QStringLiteral("dndManuallyEnabled"), QStringLiteral("0"));
    values.insert(QStringLiteral("syncEnabled"), true);
    watch.replyNext(QStringLiteral("QuietTimeSettings"), DelayedPebble::healthParamsArguments(values));
    QTRY_VERIFY(pebble.quietTimeReady());
    QVERIFY(!pebble.quietTimeBusy());
    QCOMPARE(pebble.quietTimeSettings(), values);
    pebble.setQuietTimeSetting(QStringLiteral("dndManuallyEnabled"), QStringLiteral("1"));
    QVERIFY(pebble.quietTimeBusy());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetQuietTimeSetting")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("SetQuietTimeSetting"), 0),
             QVariantList() << QStringLiteral("dndManuallyEnabled") << QStringLiteral("1"));
    watch.replyNext(QStringLiteral("SetQuietTimeSetting"), QVariantList() << false);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("QuietTimeSettings")), 1);
    watch.replyNext(QStringLiteral("QuietTimeSettings"), DelayedPebble::healthParamsArguments(values));
    QTRY_VERIFY(!pebble.quietTimeBusy());
    QVERIFY(!pebble.quietTimeError().isEmpty());
    QCOMPARE(pebble.quietTimeSettings(), values);
    watch.emitPebbleSignal(QStringLiteral("QuietTimeSettingsChanged"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("QuietTimeSettings")), 1);
    values[QStringLiteral("dndManuallyEnabled")] = QStringLiteral("1");
    watch.replyNext(QStringLiteral("QuietTimeSettings"), DelayedPebble::healthParamsArguments(values));
    QTRY_COMPARE(pebble.quietTimeSettings(), values);
    pebble.setQuietTimeSetting(QStringLiteral("dndManuallyEnabled"), QStringLiteral("0"));
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("SetQuietTimeSetting")), 1);
    watch.replyNext(QStringLiteral("SetQuietTimeSetting"), QVariantList() << true);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("QuietTimeSettings")), 1);
    values[QStringLiteral("dndManuallyEnabled")] = QStringLiteral("0");
    watch.replyNext(QStringLiteral("QuietTimeSettings"), DelayedPebble::healthParamsArguments(values));
    QTRY_VERIFY(!pebble.quietTimeBusy());
    QCOMPARE(pebble.quietTimeSettings(), values);
    QVERIFY(pebble.quietTimeError().isEmpty());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::quietTimeIgnoresOldOwnerReply()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-quiet-owner"));
    DelayedPebble watch(connection);
    watch.defer(QStringLiteral("QuietTimeSettings"));
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    pebble.refreshQuietTime();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("QuietTimeSettings")), 1);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
    QTRY_VERIFY(!pebble.quietTimeBusy());
    watch.replyNext(QStringLiteral("QuietTimeSettings"), DelayedPebble::healthParamsArguments(QVariantMap()));
    QTest::qWait(50);
    QVERIFY(!pebble.quietTimeReady());
    registerService(connection);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("QuietTimeSettings")), 1);
    watch.replyNext(QStringLiteral("QuietTimeSettings"), DelayedPebble::healthParamsArguments(QVariantMap()));
    QTRY_VERIFY(pebble.quietTimeReady());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::timelineWindowAcceptsNegativeEnd()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-negative-end"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setTimelineWindowWireValues(-10, -60, -2);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));

    QTRY_VERIFY(pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 10);
    QCOMPARE(pebble.timelineWindowFade(), 60);
    QCOMPARE(pebble.timelineWindowEnd(), -2);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::thirdQueuedTimelineWriteReplacesSecond()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-third-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    watch.defer(QStringLiteral("setTimelineWindow"));
    watch.defer(QStringLiteral("timelineWindowStart"));
    watch.defer(QStringLiteral("timelineWindowFade"));
    watch.defer(QStringLiteral("timelineWindowEnd"));

    pebble.setTimelineWindow(2, 3, 4);
    pebble.setTimelineWindow(20, 30, 40);
    pebble.setTimelineWindow(200, 300, 50);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("setTimelineWindow"), 0),
             QVariantList() << -2 << -3 << 4);
    watch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    QCOMPARE(watch.arguments(QStringLiteral("setTimelineWindow"), 1),
             QVariantList() << -200 << -300 << 50);
    QCOMPARE(watch.receivedCount(QStringLiteral("setTimelineWindow")), 2);

    watch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -200);
    watch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -300);
    watch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 50);
    QTRY_COMPARE(pebble.timelineWindowStart(), 200);
    QTRY_COMPARE(pebble.timelineWindowFade(), 300);
    QTRY_COMPARE(pebble.timelineWindowEnd(), 50);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::refreshTimelineWindowWaitsForInFlightWrite()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-refresh-write"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    watch.defer(QStringLiteral("setTimelineWindow"));
    watch.defer(QStringLiteral("timelineWindowStart"));
    watch.defer(QStringLiteral("timelineWindowFade"));
    watch.defer(QStringLiteral("timelineWindowEnd"));

    pebble.setTimelineWindow(12, 34, 5);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    pebble.refreshTimelineWindow();
    QTest::qWait(50);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 0);
    QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 0);

    watch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -12);
    watch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -34);
    watch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 5);
    QTRY_VERIFY(pebble.timelineWindowReady());
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::ownerReplacementRejectsOldTimelineReadback()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-readback-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    firstWatch.defer(QStringLiteral("setTimelineWindow"));
    firstWatch.defer(QStringLiteral("timelineWindowStart"));
    firstWatch.defer(QStringLiteral("timelineWindowFade"));
    firstWatch.defer(QStringLiteral("timelineWindowEnd"));
    pebble.setTimelineWindow(12, 34, 5);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    firstWatch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-readback-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.setTimelineWindowWireValues(-20, -30, -4);
    secondWatch.defer(QStringLiteral("timelineWindowStart"));
    secondWatch.defer(QStringLiteral("timelineWindowFade"));
    secondWatch.defer(QStringLiteral("timelineWindowEnd"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);

    firstWatch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -12);
    firstWatch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -34);
    firstWatch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 5);
    QTest::qWait(50);
    QVERIFY(!pebble.timelineWindowReady());
    secondWatch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -20);
    secondWatch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -30);
    secondWatch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << -4);
    QTRY_VERIFY(pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 20);
    QCOMPARE(pebble.timelineWindowFade(), 30);
    QCOMPARE(pebble.timelineWindowEnd(), -4);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::ownerReplacementCannotRestoreOldTimelineSnapshotAfterReadFailures()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-owner-fallback-old"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.setTimelineWindowWireValues(-10, -20, -3);
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 10);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-owner-fallback-new"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    secondWatch.defer(QStringLiteral("timelineWindowStart"));
    secondWatch.defer(QStringLiteral("timelineWindowFade"));
    secondWatch.defer(QStringLiteral("timelineWindowEnd"));
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    secondWatch.replyPendingError(QStringLiteral("timelineWindowStart"), 0);
    secondWatch.replyPendingError(QStringLiteral("timelineWindowFade"), 0);
    secondWatch.replyPendingError(QStringLiteral("timelineWindowEnd"), 0);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    secondWatch.replyPendingError(QStringLiteral("timelineWindowStart"), 0);
    secondWatch.replyPendingError(QStringLiteral("timelineWindowFade"), 0);
    secondWatch.replyPendingError(QStringLiteral("timelineWindowEnd"), 0);
    QTest::qWait(400);
    QVERIFY(!pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 10);
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowStart")), 0);
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowFade")), 0);
    QCOMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowEnd")), 0);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::timelineWindowReadFailuresAreBoundedAndRetainValidatedSnapshot()
{
    {
        QDBusConnection connection = QDBusConnection::connectToBus(
            QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-cold-read-failure"));
        QVERIFY(connection.isConnected());
        DelayedPebble watch(connection);
        watch.defer(QStringLiteral("timelineWindowStart"));
        watch.defer(QStringLiteral("timelineWindowFade"));
        watch.defer(QStringLiteral("timelineWindowEnd"));
        registerWatch(&watch, connection);
        registerService(connection);
        Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
        QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
        QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
        QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
        watch.replyPendingError(QStringLiteral("timelineWindowStart"), 0);
        watch.replyPendingError(QStringLiteral("timelineWindowFade"), 0);
        watch.replyPendingError(QStringLiteral("timelineWindowEnd"), 0);
        QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
        QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
        QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
        watch.replyPendingError(QStringLiteral("timelineWindowStart"), 0);
        watch.replyPendingError(QStringLiteral("timelineWindowFade"), 0);
        watch.replyPendingError(QStringLiteral("timelineWindowEnd"), 0);
        QTest::qWait(400);
        QVERIFY(!pebble.timelineWindowReady());
        QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 0);
        QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 0);
        QCOMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 0);
        QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
    }

    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-retained-read-failure"));
    QVERIFY(connection.isConnected());
    DelayedPebble watch(connection);
    watch.setTimelineWindowWireValues(-10, -20, -3);
    registerWatch(&watch, connection);
    registerService(connection);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    watch.defer(QStringLiteral("timelineWindowStart"));
    watch.defer(QStringLiteral("timelineWindowFade"));
    watch.defer(QStringLiteral("timelineWindowEnd"));
    pebble.refreshTimelineWindow();
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyPendingError(QStringLiteral("timelineWindowStart"), 0);
    watch.replyPendingError(QStringLiteral("timelineWindowFade"), 0);
    watch.replyPendingError(QStringLiteral("timelineWindowEnd"), 0);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(watch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    watch.replyPendingError(QStringLiteral("timelineWindowStart"), 0);
    watch.replyPendingError(QStringLiteral("timelineWindowFade"), 0);
    watch.replyPendingError(QStringLiteral("timelineWindowEnd"), 0);
    QTRY_VERIFY(pebble.timelineWindowReady());
    QCOMPARE(pebble.timelineWindowStart(), 10);
    QCOMPARE(pebble.timelineWindowFade(), 20);
    QCOMPARE(pebble.timelineWindowEnd(), -3);
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebbleAsyncTest::oldOwnerTimelineActionRepliesAreIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedPebble firstWatch(firstConnection);
    firstWatch.defer(QStringLiteral("resetTimeline"));
    firstWatch.defer(QStringLiteral("setTimelineWindow"));
    registerWatch(&firstWatch, firstConnection);
    registerService(firstConnection, QDBusConnectionInterface::DontQueueService,
                    QDBusConnectionInterface::AllowReplacement);
    Pebble pebble(QDBusObjectPath(QString::fromLatin1(watchPath)));
    QTRY_VERIFY(pebble.timelineWindowReady());
    pebble.resetTimeline();
    pebble.setTimelineWindow(1, 2, 3);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("resetTimeline")), 1);
    QTRY_COMPARE(firstWatch.pendingCount(QStringLiteral("setTimelineWindow")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebble-async-timeline-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedPebble secondWatch(secondConnection);
    registerWatch(&secondWatch, secondConnection);
    registerService(secondConnection, QDBusConnectionInterface::ReplaceExistingService,
                    QDBusConnectionInterface::DontAllowReplacement);
    QTest::qWait(50);
    const int startRequests = secondWatch.receivedCount(QStringLiteral("timelineWindowStart"));
    const int fadeRequests = secondWatch.receivedCount(QStringLiteral("timelineWindowFade"));
    const int endRequests = secondWatch.receivedCount(QStringLiteral("timelineWindowEnd"));

    firstWatch.replyNext(QStringLiteral("resetTimeline"), QVariantList());
    firstWatch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTest::qWait(50);
    QCOMPARE(secondWatch.receivedCount(QStringLiteral("timelineWindowStart")), startRequests);
    QCOMPARE(secondWatch.receivedCount(QStringLiteral("timelineWindowFade")), fadeRequests);
    QCOMPARE(secondWatch.receivedCount(QStringLiteral("timelineWindowEnd")), endRequests);

    secondWatch.defer(QStringLiteral("setTimelineWindow"));
    secondWatch.defer(QStringLiteral("timelineWindowStart"));
    secondWatch.defer(QStringLiteral("timelineWindowFade"));
    secondWatch.defer(QStringLiteral("timelineWindowEnd"));
    pebble.setTimelineWindow(9, 8, 7);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("setTimelineWindow")), 1);
    secondWatch.replyNext(QStringLiteral("setTimelineWindow"), QVariantList());
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowStart")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowFade")), 1);
    QTRY_COMPARE(secondWatch.pendingCount(QStringLiteral("timelineWindowEnd")), 1);
    secondWatch.replyNext(QStringLiteral("timelineWindowStart"), QVariantList() << -9);
    secondWatch.replyNext(QStringLiteral("timelineWindowFade"), QVariantList() << -8);
    secondWatch.replyNext(QStringLiteral("timelineWindowEnd"), QVariantList() << 7);
    QTRY_COMPARE(pebble.timelineWindowStart(), 9);
    QTRY_COMPARE(pebble.timelineWindowFade(), 8);
    QTRY_COMPARE(pebble.timelineWindowEnd(), 7);
    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}
}

void PebbleAsyncTest::notificationIconsResolveInstalledApplications()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applications = directory.path() + QStringLiteral("/applications");
    QVERIFY(QDir().mkpath(applications));
    const auto writeDesktop = [&](const QString &id, const QByteArray &contents) {
        QFile file(applications + QLatin1Char('/') + id + QStringLiteral(".desktop"));
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    };
    QVERIFY(writeDesktop(QStringLiteral("harbour-example"),
                         "[Desktop Entry]\nName=Example App\nIcon=example-icon\n"));
    QVERIFY(writeDesktop(QStringLiteral("android-launcher"),
                         "[Desktop Entry]\nName=Android App\nX-apkd-packageName=com.example.android\n"
                         "Icon=/home/test/android-icon.png\n"));
    QVERIFY(writeDesktop(QStringLiteral("jolla-messages"),
                         "[Desktop Entry]\nName=Messages\nIcon=icon-launcher-messaging\n"));
    QVERIFY(writeDesktop(QStringLiteral("hidden-app"),
                         "[Desktop Entry]\nName=Hidden\nIcon=hidden-icon\nHidden=true\n"));

    const QByteArray oldHome = qgetenv("XDG_DATA_HOME");
    const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
    qputenv("XDG_DATA_HOME", directory.path().toUtf8());
    qputenv("XDG_DATA_DIRS", directory.path().toUtf8());
    NotificationSourceModel model;
    model.insert(QStringLiteral("example"), QString(), QString(), 2);
    model.insert(QStringLiteral("com.example.android"), QString(), QString(), 2);
    model.insert(QStringLiteral("commhistoryd"), QString(), QString(), 2);
    model.insert(QStringLiteral("other-service"), QStringLiteral("Example App"), QString(), 2);
    model.insert(QStringLiteral("hidden-app"), QString(), QString(), 2);
    if (oldHome.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", oldHome);
    if (oldDirs.isNull()) qunsetenv("XDG_DATA_DIRS"); else qputenv("XDG_DATA_DIRS", oldDirs);

    QCOMPARE(model.data(model.index(0), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("example-icon"));
    QCOMPARE(model.data(model.index(1), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("/home/test/android-icon.png"));
    QCOMPARE(model.data(model.index(2), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("icon-launcher-messaging"));
    QCOMPARE(model.data(model.index(3), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("example-icon"));
    QCOMPARE(model.data(model.index(4), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("icon-lock-information"));
    model.insert(QStringLiteral("example"), QString(), QStringLiteral("provided-icon"), 2);
    QCOMPARE(model.data(model.index(0), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("provided-icon"));
    model.insert(QStringLiteral("example"), QString(), QString(), 2);
    QCOMPARE(model.data(model.index(0), NotificationSourceModel::RoleIcon).toString(), QStringLiteral("example-icon"));
}

QTEST_MAIN(PebbleAsyncTest)
#include "pebble_async_test.moc"
