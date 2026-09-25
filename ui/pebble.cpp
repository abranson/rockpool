#include "pebble.h"
#include "notificationsourcemodel.h"
#include "applicationsmodel.h"
#include "screenshotmodel.h"
#include "rockpoolaccount.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QDate>
#include <QTimer>

namespace {
const char ROCKPOOL_SERVICE[] = "org.rockpool";
const char WEATHER_UNITS[] = "WeatherUnits";
const char WEATHER_LANGUAGE[] = "WeatherLanguage";
const char WEATHER_ALT_KEY[] = "WeatherAltKey";
const char WEATHER_LOCATIONS[] = "WeatherLocations";
const char DEV_CONNECTION_ENABLED[] = "DevConnectionEnabled";
const char DEV_CONNECTION_STATE[] = "DevConnectionState";
const char LOG_LEVEL[] = "getLogLevel";
const char TIMELINE_COLORS[] = "TimelineColors";
const char TIMELINE_ICONS[] = "TimelineIcons";
const char IMPERIAL_UNITS[] = "ImperialUnits";
const char PROFILE_WHEN_CONNECTED[] = "ProfileWhenConnected";
const char PROFILE_WHEN_DISCONNECTED[] = "ProfileWhenDisconnected";
const char CALENDAR_SYNC_ENABLED[] = "CalendarSyncEnabled";
const char SYNC_APPS_FROM_CLOUD[] = "syncAppsFromCloud";
const char HEALTH_PARAMS[] = "HealthParams";
const char CANNED_RESPONSES[] = "cannedResponses";
const char FAVORITE_CONTACTS[] = "getFavoriteContacts";

QStringList weatherProperties()
{
    return QStringList()
        << QString::fromLatin1(WEATHER_UNITS)
        << QString::fromLatin1(WEATHER_LANGUAGE)
        << QString::fromLatin1(WEATHER_ALT_KEY)
        << QString::fromLatin1(WEATHER_LOCATIONS);
}

QStringList developerProperties()
{
    return QStringList()
        << QString::fromLatin1(DEV_CONNECTION_ENABLED)
        << QString::fromLatin1(DEV_CONNECTION_STATE)
        << QString::fromLatin1(LOG_LEVEL);
}

QStringList settingsPageProperties()
{
    return QStringList()
        << QString::fromLatin1(IMPERIAL_UNITS)
        << QString::fromLatin1(PROFILE_WHEN_CONNECTED)
        << QString::fromLatin1(PROFILE_WHEN_DISCONNECTED)
        << QString::fromLatin1(CALENDAR_SYNC_ENABLED)
        << QString::fromLatin1(SYNC_APPS_FROM_CLOUD);
}

QStringList bootstrapProperties()
{
    return QStringList()
        << QStringLiteral("Name")
        << QStringLiteral("Address")
        << QStringLiteral("SerialNumber")
        << QStringLiteral("PlatformString")
        << QStringLiteral("HardwarePlatform")
        << QStringLiteral("SoftwareVersion")
        << QStringLiteral("LanguageVersion")
        << QStringLiteral("Model")
        << QStringLiteral("Recovery")
        << QStringLiteral("IsConnected");
}

QStringList timelineWindowProperties()
{
    return QStringList()
        << QStringLiteral("timelineWindowStart")
        << QStringLiteral("timelineWindowFade")
        << QStringLiteral("timelineWindowEnd");
}

QStringList firmwareProperties()
{
    return QStringList()
        << QStringLiteral("FirmwareUpgradeAvailable")
        << QStringLiteral("FirmwareReleaseNotes")
        << QStringLiteral("CandidateFirmwareVersion")
        << QStringLiteral("UpgradingFirmware");
}

bool decodeVariantMap(const QVariant &value, QVariantMap *map)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return decodeVariantMap(value.value<QDBusVariant>().variant(), map);
    }
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument >> *map;
        return true;
    }
    if (value.type() == QVariant::Map) {
        *map = value.toMap();
        return true;
    }
    return false;
}

QVariant unwrapDBusVariant(const QVariant &value)
{
    QVariant decoded = value;
    while (decoded.userType() == qMetaTypeId<QDBusVariant>()) {
        decoded = decoded.value<QDBusVariant>().variant();
    }
    return decoded;
}

QVariantMap unwrapVariantMapValues(const QVariantMap &map)
{
    QVariantMap decoded;
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        decoded.insert(it.key(), unwrapDBusVariant(it.value()));
    }
    return decoded;
}

bool decodeVariantMapList(const QDBusMessage &message, QVariantList *list)
{
    if (message.type() == QDBusMessage::ErrorMessage || message.arguments().count() != 1) {
        return false;
    }

    const QVariant value = message.arguments().first();
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        const QVariantList values = value.toList();
        foreach (const QVariant &entry, values) {
            QVariantMap map;
            if (!decodeVariantMap(entry, &map)) {
                return false;
            }
            list->append(unwrapVariantMapValues(map));
        }
        return true;
    }

    const QDBusArgument argument = value.value<QDBusArgument>();
    argument.beginArray();
    while (!argument.atEnd()) {
        QVariant entry;
        argument >> entry;
        QVariantMap map;
        if (!decodeVariantMap(entry, &map)) {
            argument.endArray();
            return false;
        }
        list->append(unwrapVariantMapValues(map));
    }
    argument.endArray();
    return true;
}

bool decodeVariantMapListValue(const QVariant &value, QVariantList *list)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return decodeVariantMapListValue(value.value<QDBusVariant>().variant(), list);
    }
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        const QVariantList values = value.toList();
        foreach (const QVariant &entry, values) {
            QVariantMap map;
            if (!decodeVariantMap(entry, &map)) {
                return false;
            }
            list->append(unwrapVariantMapValues(map));
        }
        return value.type() == QVariant::List || value.userType() == qMetaTypeId<QVariantList>();
    }

    const QDBusArgument argument = value.value<QDBusArgument>();
    argument.beginArray();
    while (!argument.atEnd()) {
        QVariant entry;
        argument >> entry;
        QVariantMap map;
        if (!decodeVariantMap(entry, &map)) {
            argument.endArray();
            return false;
        }
        list->append(unwrapVariantMapValues(map));
    }
    argument.endArray();
    return true;
}

bool isIntegerValue(const QVariant &value)
{
    const QVariant decoded = unwrapDBusVariant(value);
    return decoded.type() == QVariant::Int || decoded.type() == QVariant::UInt
        || decoded.type() == QVariant::LongLong || decoded.type() == QVariant::ULongLong;
}

bool validHealthWeek(const QVariantList &week, const QStringList &integerKeys)
{
    if (week.size() != 7) {
        return false;
    }
    foreach (const QVariant &entry, week) {
        const QVariantMap record = entry.toMap();
        if (record.value(QStringLiteral("label")).type() != QVariant::String
                || record.value(QStringLiteral("date")).type() != QVariant::String) {
            return false;
        }
        foreach (const QString &key, integerKeys) {
            if (!record.contains(key) || !isIntegerValue(record.value(key))) {
                return false;
            }
        }
    }
    return true;
}

bool decodeHealthOverview(const QDBusMessage &message, QVariantMap *overview)
{
    if (message.type() == QDBusMessage::ErrorMessage || message.arguments().count() != 1
            || !decodeVariantMap(message.arguments().first(), overview)) {
        return false;
    }

    const QStringList integerKeys = QStringList()
        << QStringLiteral("todaySteps")
        << QStringLiteral("averageStepsPerDay")
        << QStringLiteral("lastNightSleepSeconds")
        << QStringLiteral("lastNightDeepSleepSeconds")
        << QStringLiteral("averageSleepSecondsPerDay")
        << QStringLiteral("todayAverageHeartRate")
        << QStringLiteral("todayMaxHeartRate")
        << QStringLiteral("latestHeartRate")
        << QStringLiteral("latestHeartRateTimestamp")
        << QStringLiteral("averageHeartRate30Days")
        << QStringLiteral("latestDataTimestamp")
        << QStringLiteral("daysOfData");
    foreach (const QString &key, integerKeys) {
        if (!overview->contains(key) || !isIntegerValue(overview->value(key))) {
            return false;
        }
        overview->insert(key, unwrapDBusVariant(overview->value(key)));
    }

    QVariantList stepsWeek;
    QVariantList sleepWeek;
    if (!decodeVariantMapListValue(overview->value(QStringLiteral("stepsWeek")), &stepsWeek)
            || !decodeVariantMapListValue(
                overview->value(QStringLiteral("sleepWeek")), &sleepWeek)
            || !validHealthWeek(
                stepsWeek,
                QStringList() << QStringLiteral("steps")
                              << QStringLiteral("averageHeartRate")
                              << QStringLiteral("maxHeartRate"))
            || !validHealthWeek(
                sleepWeek,
                QStringList() << QStringLiteral("sleepDuration")
                              << QStringLiteral("deepSleepDuration"))) {
        return false;
    }
    overview->insert(QStringLiteral("stepsWeek"), stepsWeek);
    overview->insert(QStringLiteral("sleepWeek"), sleepWeek);
    // Optional for compatibility with an older daemon. Reject malformed history rather than
    // publishing a partly decoded snapshot to QML.
    if (overview->contains(QStringLiteral("history"))) {
        QVariantList history;
        if (!decodeVariantMapListValue(overview->value(QStringLiteral("history")), &history)
                || history.isEmpty() || history.size() > 90) {
            return false;
        }
        QDate previous;
        foreach (const QVariant &entry, history) {
            const QVariantMap record = entry.toMap();
            const QString dateText = record.value(QStringLiteral("date")).toString();
            const QDate date = QDate::fromString(dateText, Qt::ISODate);
            if (!date.isValid() || date.toString(Qt::ISODate) != dateText
                    || (previous.isValid() && previous.addDays(1) != date)) {
                return false;
            }
            previous = date;
            const QStringList keys = QStringList() << QStringLiteral("steps")
                << QStringLiteral("sleepDuration") << QStringLiteral("deepSleepDuration")
                << QStringLiteral("hasMovement") << QStringLiteral("hasSleep");
            foreach (const QString &key, keys) {
                if (!record.contains(key) || !isIntegerValue(record.value(key))
                        || record.value(key).toLongLong() < 0) {
                    return false;
                }
            }
            if (record.value(QStringLiteral("hasMovement")).toLongLong() > 1
                    || record.value(QStringLiteral("hasSleep")).toLongLong() > 1) {
                return false;
            }
        }
        overview->insert(QStringLiteral("history"), history);
    }
    return true;
}

bool isNonEmptyString(const QVariantMap &map, const QString &key)
{
    const QVariant value = map.value(key);
    return value.type() == QVariant::String && !value.toString().trimmed().isEmpty();
}

bool isRgbString(const QVariantMap &map)
{
    const QVariant value = map.value(QStringLiteral("rgb"));
    if (value.type() != QVariant::String) {
        return false;
    }
    const QString rgb = value.toString();
    if (rgb.size() != 7 || rgb.at(0) != QLatin1Char('#')) {
        return false;
    }
    for (int i = 1; i < rgb.size(); ++i) {
        const QChar c = rgb.at(i);
        if (!(c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                && !(c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                && !(c >= QLatin1Char('A') && c <= QLatin1Char('F'))) {
            return false;
        }
    }
    return true;
}

bool validateTimelinePalette(const QString &method, const QVariantList &values)
{
    foreach (const QVariant &value, values) {
        if (value.type() != QVariant::Map) {
            return false;
        }
        const QVariantMap map = value.toMap();
        if (method == QString::fromLatin1(TIMELINE_COLORS)) {
            if (!isNonEmptyString(map, QStringLiteral("name")) || !isRgbString(map)) {
                return false;
            }
        } else if (method == QString::fromLatin1(TIMELINE_ICONS)) {
            if (!isNonEmptyString(map, QStringLiteral("name"))
                    || !isNonEmptyString(map, QStringLiteral("code"))) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool decodeStringList(const QVariant &value, QStringList *list)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return decodeStringList(value.value<QDBusVariant>().variant(), list);
    }
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        *list = value.toStringList();
        return value.canConvert(QVariant::StringList);
    }

    const QDBusArgument argument = value.value<QDBusArgument>();
    argument.beginArray();
    while (!argument.atEnd()) {
        QString entry;
        argument >> entry;
        list->append(entry);
    }
    argument.endArray();
    return true;
}

bool decodeStringMap(const QVariant &value, QVariantMap *map)
{
    QVariantMap encoded;
    if (!decodeVariantMap(value, &encoded)) {
        return false;
    }

    QVariantMap decoded;
    for (QVariantMap::const_iterator it = encoded.constBegin();
         it != encoded.constEnd(); ++it) {
        QStringList strings;
        if (!decodeStringList(it.value(), &strings)) {
            return false;
        }
        decoded.insert(it.key(), strings);
    }
    *map = decoded;
    return true;
}

QVariant unwrapDbusVariant(const QVariant &value)
{
    QVariant unwrapped = value;
    while (unwrapped.userType() == qMetaTypeId<QDBusVariant>()) {
        unwrapped = unwrapped.value<QDBusVariant>().variant();
    }
    return unwrapped;
}

bool healthParamsSnapshotValid(const QVariantMap &params)
{
    const QStringList booleanKeys = QStringList()
        << QStringLiteral("enabled")
        << QStringLiteral("moreActive")
        << QStringLiteral("sleepMore");
    foreach (const QString &key, booleanKeys) {
        if (!params.contains(key) || params.value(key).type() != QVariant::Bool) {
            return false;
        }
    }

    const QStringList integerKeys = QStringList()
        << QStringLiteral("age")
        << QStringLiteral("height")
        << QStringLiteral("weight");
    foreach (const QString &key, integerKeys) {
        bool valid = false;
        params.value(key).toInt(&valid);
        if (!params.contains(key) || !valid) {
            return false;
        }
    }

    const QString gender = params.value(QStringLiteral("gender")).toString();
    return params.contains(QStringLiteral("gender"))
        && (gender == QStringLiteral("female") || gender == QStringLiteral("male"));
}

bool decodeHealthParams(const QVariant &value, QVariantMap *params)
{
    QVariantMap encoded;
    if (!decodeVariantMap(value, &encoded)) {
        return false;
    }

    QVariantMap decoded;
    for (QVariantMap::const_iterator it = encoded.constBegin();
         it != encoded.constEnd(); ++it) {
        decoded.insert(it.key(), unwrapDbusVariant(it.value()));
    }
    if (!healthParamsSnapshotValid(decoded)) {
        return false;
    }
    *params = decoded;
    return true;
}

bool normalizeStringMap(const QVariantMap &values, QVariantMap *map)
{
    QVariantMap normalized;
    for (QVariantMap::const_iterator it = values.constBegin();
         it != values.constEnd(); ++it) {
        QStringList strings;
        const QVariant value = it.value();
        if (value.type() == QVariant::StringList) {
            strings = value.toStringList();
        } else if (value.type() == QVariant::Map) {
            foreach (const QVariant &entry, value.toMap().values()) {
                strings.append(entry.toString());
            }
        } else if (value.type() == QVariant::List) {
            foreach (const QVariant &entry, value.toList()) {
                strings.append(entry.toString());
            }
        } else {
            return false;
        }
        normalized.insert(it.key(), strings);
    }
    *map = normalized;
    return true;
}

bool decodeWeatherLocations(const QDBusMessage &message, QVariantList *locations)
{
    if (message.type() == QDBusMessage::ErrorMessage || message.arguments().count() != 1) {
        return false;
    }

    const QVariant value = message.arguments().first();
    QVariantList entries;
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument.beginArray();
        while (!argument.atEnd()) {
            QVariant entry;
            argument >> entry;
            entries.append(entry);
        }
        argument.endArray();
    } else {
        entries = value.toList();
    }

    foreach (const QVariant &entry, entries) {
        QStringList fields;
        if (!decodeStringList(entry, &fields) || fields.count() != 3) {
            return false;
        }
        locations->append(fields);
    }
    return true;
}
}

RockpoolPebbleInterface::RockpoolPebbleInterface(const QString &path, QObject *parent):
    QDBusAbstractInterface(
        QString::fromLatin1(ROCKPOOL_SERVICE),
        path,
        "org.rockpool.Pebble",
        QDBusConnection::sessionBus(),
        parent)
{
}

// TODO: Bootstrapping config from
// https://boot.getpebble.com/api/config/android/v3/1055?locale=de_DE&app_version=3.13.0-1055-06644a6
Pebble::Pebble(const QDBusObjectPath &path, QObject *parent,
               RockpoolAccount *account):
    QObject(parent),
    m_path(path)
{
    m_iface = new RockpoolPebbleInterface(path.path(), this);
    m_notifications = new NotificationSourceModel(this);
    m_installedApps = new ApplicationsModel(this);
    connect(m_installedApps, &ApplicationsModel::appsSorted, this, &Pebble::appsSorted);
    m_installedWatchfaces = new ApplicationsModel(this);
    connect(m_installedWatchfaces, &ApplicationsModel::appsSorted, this, &Pebble::appsSorted);
    m_screenshotModel = new ScreenshotModel(this);
    m_account = account ? account : new RockpoolAccount(this);
    connect(m_account, &RockpoolAccount::authenticatedChanged,
            this, &Pebble::accountAuthenticatedChanged);
    connect(m_account, &RockpoolAccount::nameChanged,
            this, &Pebble::accountNameChanged);
    connect(m_account, &RockpoolAccount::emailChanged,
            this, &Pebble::accountEmailChanged);
    connect(m_account, &RockpoolAccount::tokenPendingChanged,
            this, &Pebble::accountTokenPendingChanged);
    connect(m_account, &RockpoolAccount::tokenErrorChanged,
            this, &Pebble::accountTokenErrorChanged);
    m_serviceWatcher = new QDBusServiceWatcher(
        QString::fromLatin1(ROCKPOOL_SERVICE), QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &Pebble::serviceOwnerChanged);

    connect(m_iface, &RockpoolPebbleInterface::Connected, this, &Pebble::pebbleConnected);
    connect(m_iface, &RockpoolPebbleInterface::Disconnected, this, &Pebble::pebbleDisconnected);
    connect(m_iface, &RockpoolPebbleInterface::ConnectionStateChanged,
            this, &Pebble::pebbleConnectionStateChanged);
    connect(m_iface, &RockpoolPebbleInterface::InstalledAppsChanged,
            this, &Pebble::refreshApps);
    connect(m_iface, &RockpoolPebbleInterface::OpenURL, this, &Pebble::openURL);
    connect(m_iface, &RockpoolPebbleInterface::NotificationFilterChanged,
            this, &Pebble::notificationFilterChanged);
    connect(m_iface, &RockpoolPebbleInterface::ScreenshotAdded,
            this, &Pebble::screenshotAdded);
    connect(m_iface, &RockpoolPebbleInterface::ScreenshotRemoved,
            this, &Pebble::screenshotRemoved);
    connect(m_iface, &RockpoolPebbleInterface::FirmwareUpgradeAvailableChanged,
            this, &Pebble::refreshFirmwareUpdateInfo);
    connect(m_iface, &RockpoolPebbleInterface::LanguageVersionChanged,
            this, &Pebble::refreshLanguageVersion);
    connect(m_iface, &RockpoolPebbleInterface::UpgradingFirmwareChanged,
            this, &Pebble::refreshFirmwareUpdateInfo);
    connect(m_iface, &RockpoolPebbleInterface::LogsDumped,
            this, &Pebble::logsDumpedFromService);
    connect(m_iface, &RockpoolPebbleInterface::HealthParamsChanged,
            this, &Pebble::healthParamsChangedFromService);
    connect(m_iface, &RockpoolPebbleInterface::HealthDataChanged,
            this, &Pebble::healthDataChangedFromService);
    connect(m_iface, &RockpoolPebbleInterface::ImperialUnitsChanged,
            this, [this]() {
                settingsPropertyChangedFromService(QString::fromLatin1(IMPERIAL_UNITS));
                if (m_weatherRequested) {
                    ++m_weatherValueRevisions[QString::fromLatin1(WEATHER_UNITS)];
                    requestWeatherProperty(QString::fromLatin1(WEATHER_UNITS));
                }
            });
    connect(m_iface, &RockpoolPebbleInterface::ProfileWhenConnectedChanged,
            this, [this]() {
                settingsPropertyChangedFromService(
                    QString::fromLatin1(PROFILE_WHEN_CONNECTED));
            });
    connect(m_iface, &RockpoolPebbleInterface::ProfileWhenDisconnectedChanged,
            this, [this]() {
                settingsPropertyChangedFromService(
                    QString::fromLatin1(PROFILE_WHEN_DISCONNECTED));
            });
    connect(m_iface, &RockpoolPebbleInterface::CalendarSyncEnabledChanged,
            this, [this]() {
                settingsPropertyChangedFromService(
                    QString::fromLatin1(CALENDAR_SYNC_ENABLED));
            });
    connect(m_iface, &RockpoolPebbleInterface::DevConnectionChanged,
            this, &Pebble::devConStateChanged);
    connect(m_iface, &RockpoolPebbleInterface::WeatherLocationsChanged,
            this, &Pebble::weatherLocationsChangedFromService);

    connect(m_iface, &RockpoolPebbleInterface::QuietTimeSettingsChanged,
            this, [this]() {
                if (m_quietTimeRequested) refreshQuietTime();
            });

    dataChanged();
    refreshApps();
    refreshNotifications();
    refreshScreenshots();
    refreshFirmwareUpdateInfo();
}

void Pebble::serviceOwnerChanged(const QString &service,
                                 const QString &oldOwner,
                                 const QString &newOwner)
{
    Q_UNUSED(service)
    Q_UNUSED(oldOwner)

    const bool logDumpWasPending = m_logDumpPending;
    m_logDumpPending = false;
    ++m_logDumpEpoch;
    ++m_serviceEpoch;
    m_quietTimeSettings.clear();
    m_quietTimeReady = false;
    m_quietTimeBusy = false;
    m_quietTimeRefreshPending = false;
    m_quietTimeError.clear();
    emit quietTimeChanged();
    if (!newOwner.isEmpty() && m_quietTimeRequested) refreshQuietTime();
    ++m_connectionEpoch;
    ++m_appsEpoch;
    ++m_screenshotsEpoch;
    ++m_firmwareEpoch;
    ++m_notificationFiltersEpoch;
    ++m_timelineColorsEpoch;
    ++m_timelineIconsEpoch;
    ++m_timelineWindowRequestEpoch;
    ++m_timelineWindowWriteEpoch;
    ++m_weatherLocationsEpoch;
    foreach (const QString &propertyName, weatherProperties()) {
        ++m_weatherWriteEpochs[propertyName];
    }
    foreach (const QString &propertyName, developerProperties()) {
        ++m_developerWriteEpochs[propertyName];
    }
    foreach (const QString &propertyName, settingsPageProperties()) {
        ++m_settingsWriteEpochs[propertyName];
    }
    ++m_healthParamsWriteEpoch;
    ++m_healthParamsValueRevision;
    ++m_healthOverviewEpoch;
    ++m_healthSyncEpoch;
    ++m_cannedResponsesWriteEpoch;
    ++m_cannedContactsRequestEpoch;
    ++m_cannedContactsWriteEpoch;
    ++m_cannedContactsValueRevision;
    m_addressReadFailures = 0;
    m_weatherLoadedProperties.clear();
    setWeatherSettingsReady(false);
    m_developerLoadedProperties.clear();
    setDeveloperSettingsReady(false);
    m_settingsLoadedProperties.clear();
    m_settingsAuthoritativeProperties.clear();
    m_settingsReadFailures.clear();
    setSettingsPageReady(false);
    m_healthParamsAuthoritative = false;
    m_healthParamsValid = false;
    m_healthParamsReadFailures = 0;
    setHealthParamsReady(false);
    if (!m_healthParams.isEmpty()) {
        m_healthParams.clear();
        emit healthParamsChanged();
    }
    setHealthOverviewReady(false);
    if (!m_healthOverview.isEmpty()) {
        m_healthOverview.clear();
        emit healthOverviewChanged();
    }
    if (m_healthSyncing) {
        setHealthSyncing(false);
        emit healthSyncCompleted(false);
    }
    m_cannedResponsesAuthoritative = false;
    m_cannedResponsesReadFailures = 0;
    setCannedResponsesReady(false);
    if (!m_cannedResponses.isEmpty()) {
        m_cannedResponses.clear();
        emit cannedResponsesChanged();
    }
    m_cannedContactsAuthoritative = false;
    m_cannedContactsReadFailures = 0;
    setCannedContactsReady(false);
    if (!m_cannedContacts.isEmpty()) {
        m_cannedContacts.clear();
        emit cannedContactsChanged();
    }
    m_timelineColorsInFlight = false;
    m_timelineIconsInFlight = false;
    m_timelineColorsFailures = 0;
    m_timelineIconsFailures = 0;
    if (!m_timelineColors.isEmpty()) {
        m_timelineColors.clear();
        emit timelineColorsChanged();
    }
    if (!m_timelineIcons.isEmpty()) {
        m_timelineIcons.clear();
        emit timelineIconsChanged();
    }
    setTimelinePaletteReady(QString::fromLatin1(TIMELINE_COLORS), false);
    setTimelinePaletteReady(QString::fromLatin1(TIMELINE_ICONS), false);
    m_pendingConnectionValues.clear();
    m_pendingConnectionReplies.clear();
    m_pendingFirmwareValues.clear();
    m_pendingFirmwareReplies.clear();
    m_pendingTimelineWindowValues.clear();
    m_pendingTimelineWindowReplies.clear();
    m_timelineWindowHasSnapshot = false;
    m_timelineWindowRequestFailed = false;
    m_timelineWindowReadFailures = 0;
    m_timelineWindowWriteInFlight = false;
    m_timelineWindowWriteQueued = false;
    setTimelineWindowReady(false);

    if (logDumpWasPending) {
        emit logsDumped(false);
    }

    if (newOwner.isEmpty()) {
        return;
    }

    dataChanged();
    refreshApps();
    refreshNotifications();
    refreshScreenshots();
    refreshFirmwareUpdateInfo();
    if (m_weatherRequested) {
        refreshWeatherSettings();
    }
    if (m_developerRequested) {
        refreshDeveloperSettings();
    }
    if (m_settingsPageRequested) {
        refreshSettingsPage();
    }
    if (m_healthParamsRequested) {
        refreshHealthParams();
    }
    if (m_healthOverviewRequested) {
        refreshHealthOverview();
    }
    if (m_cannedResponsesRequested) {
        refreshCannedResponses();
    }
    if (m_cannedContactsRequested) {
        refreshCannedContacts();
    }
    if (m_timelineColorsRequested) {
        refreshTimelineColors();
    }
    if (m_timelineIconsRequested) {
        refreshTimelineIcons();
    }
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
    return m_platformString;
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
    return m_languageVersion;
}

void Pebble::loadLanguagePack(const QString &pblFile)
{
    qDebug() << "Requesting to load language from" << pblFile;
    sendVoidCommand(QStringLiteral("LoadLanguagePack"), QVariantList() << pblFile);
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

QVariantMap Pebble::cannedResponses() const
{
    return m_cannedResponses;
}

bool Pebble::cannedResponsesReady() const
{
    return m_cannedResponsesReady;
}

void Pebble::refreshCannedResponses()
{
    m_cannedResponsesRequested = true;
    m_cannedResponsesAuthoritative = false;
    m_cannedResponsesReadFailures = 0;
    setCannedResponsesReady(false);
    requestProperty(QString::fromLatin1(CANNED_RESPONSES));
}

void Pebble::setCannedResponses(const QVariantMap &cans)
{
    m_cannedResponsesRequested = true;
    QVariantMap normalized;
    if (!normalizeStringMap(cans, &normalized) || normalized.isEmpty()) {
        qWarning() << "Cannot encode canned responses" << cans;
        return;
    }

    QVariantMap merged = m_cannedResponses;
    for (QVariantMap::const_iterator it = normalized.constBegin();
         it != normalized.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    if (merged == m_cannedResponses && m_cannedResponsesAuthoritative) {
        return;
    }

    ++m_propertyEpochs[QString::fromLatin1(CANNED_RESPONSES)];
    const quint64 writeEpoch = ++m_cannedResponsesWriteEpoch;
    const QVariantMap previousResponses = m_cannedResponses;
    m_cannedResponsesAuthoritative = false;
    m_cannedResponsesReadFailures = 0;
    setCannedResponsesReady(false);
    ++m_cannedResponsesValueRevision;
    const quint64 valueRevision = m_cannedResponsesValueRevision;
    if (m_cannedResponses != merged) {
        m_cannedResponses = merged;
        emit cannedResponsesChanged();
    }

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(
            QStringLiteral("setCannedResponses"), QVariantList() << normalized), this);
    watcher->setProperty("previousResponses", previousResponses);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    watcher->setProperty("valueRevision",
                         QVariant::fromValue<qulonglong>(valueRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::cannedResponsesWriteReplyFinished);
}

QVariantMap Pebble::getCannedResponses(const QStringList &keys)
{
    if (keys.isEmpty()) {
        return m_cannedResponses;
    }
    QVariantMap selected;
    foreach (const QString &key, keys) {
        if (m_cannedResponses.contains(key)) {
            selected.insert(key, m_cannedResponses.value(key));
        }
    }
    return selected;
}

void Pebble::applyCannedResponses(const QVariantMap &responses, bool authoritative)
{
    ++m_cannedResponsesValueRevision;
    m_cannedResponsesReadFailures = 0;
    m_cannedResponsesAuthoritative = authoritative;
    if (m_cannedResponses != responses) {
        m_cannedResponses = responses;
        emit cannedResponsesChanged();
    }
    setCannedResponsesReady(true);
}

void Pebble::cannedResponsesReadFailed(quint64 requestEpoch)
{
    if (!m_cannedResponsesRequested) {
        return;
    }
    ++m_cannedResponsesReadFailures;
    if (m_cannedResponsesReadFailures == 1) {
        const quint64 retryServiceEpoch = m_serviceEpoch;
        QTimer::singleShot(250, this,
                           [this, requestEpoch, retryServiceEpoch]() {
            const QString propertyName = QString::fromLatin1(CANNED_RESPONSES);
            if (retryServiceEpoch == m_serviceEpoch
                    && requestEpoch == m_propertyEpochs.value(propertyName)
                    && !m_cannedResponsesReady) {
                requestProperty(propertyName);
            }
        });
        return;
    }

    m_cannedResponsesAuthoritative = false;
    setCannedResponsesReady(true);
}

void Pebble::setCannedResponsesReady(bool ready)
{
    if (m_cannedResponsesReady == ready) {
        return;
    }
    m_cannedResponsesReady = ready;
    emit cannedResponsesReadyChanged();
}

void Pebble::cannedResponsesWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QVariantMap previousResponses =
        watcher->property("previousResponses").toMap();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    const quint64 valueRevision = watcher->property("valueRevision").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || writeEpoch != m_cannedResponsesWriteEpoch) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "setCannedResponses failed:" << reply.errorMessage();
        if (valueRevision == m_cannedResponsesValueRevision) {
            ++m_cannedResponsesValueRevision;
            if (m_cannedResponses != previousResponses) {
                m_cannedResponses = previousResponses;
                emit cannedResponsesChanged();
            }
        }
    }
    requestProperty(QString::fromLatin1(CANNED_RESPONSES));
}

void Pebble::setCannedContacts(const QVariantMap &cans)
{
    m_cannedContactsRequested = true;
    QVariantMap normalized;
    if (!normalizeStringMap(cans, &normalized)) {
        qWarning() << "Cannot encode favorite contacts" << cans;
        return;
    }
    if (normalized == m_cannedContacts && m_cannedContactsAuthoritative) {
        return;
    }

    ++m_cannedContactsRequestEpoch;
    const quint64 writeEpoch = ++m_cannedContactsWriteEpoch;
    const QVariantMap previousContacts = m_cannedContacts;
    m_cannedContactsAuthoritative = false;
    m_cannedContactsReadFailures = 0;
    setCannedContactsReady(false);
    ++m_cannedContactsValueRevision;
    const quint64 valueRevision = m_cannedContactsValueRevision;
    if (m_cannedContacts != normalized) {
        m_cannedContacts = normalized;
        emit cannedContactsChanged();
    }

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(
            QStringLiteral("setFavoriteContacts"),
            QVariantList() << normalized), this);
    watcher->setProperty("previousContacts", previousContacts);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    watcher->setProperty("valueRevision",
                         QVariant::fromValue<qulonglong>(valueRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::cannedContactsWriteReplyFinished);
}

QVariantMap Pebble::getCannedContacts(const QStringList &keys)
{
    if (keys.isEmpty()) {
        return m_cannedContacts;
    }
    QVariantMap selected;
    foreach (const QString &key, keys) {
        if (m_cannedContacts.contains(key)) {
            selected.insert(key, m_cannedContacts.value(key));
        }
    }
    return selected;
}

QVariantMap Pebble::cannedContacts() const
{
    return m_cannedContacts;
}

bool Pebble::cannedContactsReady() const
{
    return m_cannedContactsReady;
}

void Pebble::refreshCannedContacts()
{
    m_cannedContactsRequested = true;
    m_cannedContactsAuthoritative = false;
    m_cannedContactsReadFailures = 0;
    setCannedContactsReady(false);
    requestCannedContacts();
}

void Pebble::requestCannedContacts()
{
    const quint64 requestEpoch = ++m_cannedContactsRequestEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(
            QString::fromLatin1(FAVORITE_CONTACTS),
            QVariantList() << QStringList()), this);
    watcher->setProperty("requestEpoch",
                         QVariant::fromValue<qulonglong>(requestEpoch));
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::cannedContactsReadReplyFinished);
}

void Pebble::applyCannedContacts(const QVariantMap &contacts, bool authoritative)
{
    ++m_cannedContactsValueRevision;
    m_cannedContactsReadFailures = 0;
    m_cannedContactsAuthoritative = authoritative;
    if (m_cannedContacts != contacts) {
        m_cannedContacts = contacts;
        emit cannedContactsChanged();
    }
    setCannedContactsReady(true);
}

void Pebble::cannedContactsReadFailed(quint64 requestEpoch)
{
    if (!m_cannedContactsRequested) {
        return;
    }
    ++m_cannedContactsReadFailures;
    if (m_cannedContactsReadFailures == 1) {
        const quint64 retryServiceEpoch = m_serviceEpoch;
        QTimer::singleShot(250, this,
                           [this, requestEpoch, retryServiceEpoch]() {
            if (retryServiceEpoch == m_serviceEpoch
                    && requestEpoch == m_cannedContactsRequestEpoch
                    && !m_cannedContactsReady) {
                requestCannedContacts();
            }
        });
        return;
    }

    m_cannedContactsAuthoritative = false;
    setCannedContactsReady(true);
}

void Pebble::setCannedContactsReady(bool ready)
{
    if (m_cannedContactsReady == ready) {
        return;
    }
    m_cannedContactsReady = ready;
    emit cannedContactsReadyChanged();
}

void Pebble::cannedContactsReadReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch
            || requestEpoch != m_cannedContactsRequestEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage
            || reply.arguments().count() != 1) {
        qWarning() << "Could not refresh favorite contacts" << reply.errorMessage();
        cannedContactsReadFailed(requestEpoch);
        return;
    }

    QVariantMap contacts;
    if (decodeStringMap(reply.arguments().first(), &contacts)) {
        applyCannedContacts(contacts, true);
    } else {
        qWarning() << "Invalid favorite contacts" << reply.arguments().first();
        cannedContactsReadFailed(requestEpoch);
    }
}

void Pebble::cannedContactsWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QVariantMap previousContacts =
        watcher->property("previousContacts").toMap();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    const quint64 valueRevision = watcher->property("valueRevision").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch
            || writeEpoch != m_cannedContactsWriteEpoch) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "setFavoriteContacts failed:" << reply.errorMessage();
        if (valueRevision == m_cannedContactsValueRevision) {
            ++m_cannedContactsValueRevision;
            if (m_cannedContacts != previousContacts) {
                m_cannedContacts = previousContacts;
                emit cannedContactsChanged();
            }
        }
    }
    requestCannedContacts();
}

QVariantList Pebble::weatherLocations() const
{
    return m_weatherLocations;
}

void Pebble::setWeatherLocations(const QVariantList &in)
{
    QVariantList out;
    foreach (const QVariant &v, in) {
        if (v.canConvert(QVariant::StringList)) {
            out.append(v.toStringList());
        } else if (v.canConvert(QVariant::List)) {
            QStringList l;
            foreach (const QVariant &sv, v.toList()) {
                l.append(sv.toString());
            }
            out.append(l);
        }
    }
    if (out == m_weatherLocations) {
        return;
    }
    sendWeatherWrite(QString::fromLatin1(WEATHER_LOCATIONS),
                     QStringLiteral("SetWeatherLocations"), out,
                     m_weatherLocations);
}

QString Pebble::weatherUnits() const
{
    return m_weatherUnits;
}

void Pebble::setWeatherUnits(const QString &u)
{
    if (u == m_weatherUnits) {
        return;
    }
    sendWeatherWrite(QString::fromLatin1(WEATHER_UNITS),
                     QStringLiteral("setWeatherUnits"), u, m_weatherUnits);
}

QString Pebble::weatherLanguage() const
{
    return m_weatherLanguage;
}

void Pebble::setWeatherLanguage(const QString &l)
{
    if (l == m_weatherLanguage) {
        return;
    }
    sendWeatherWrite(QString::fromLatin1(WEATHER_LANGUAGE),
                     QStringLiteral("setWeatherLanguage"), l,
                     m_weatherLanguage);
}

QString Pebble::weatherAltKey() const
{
    return m_weatherAltKey;
}

void Pebble::setWeatherAltKey(const QString &key)
{
    if (key == m_weatherAltKey) {
        return;
    }
    sendWeatherWrite(QString::fromLatin1(WEATHER_ALT_KEY),
                     QStringLiteral("setWeatherAltKey"), key,
                     m_weatherAltKey);
}

bool Pebble::weatherSettingsReady() const
{
    return m_weatherSettingsReady;
}

void Pebble::refreshWeatherSettings()
{
    m_weatherRequested = true;
    m_weatherLoadedProperties.clear();
    setWeatherSettingsReady(false);
    foreach (const QString &propertyName, weatherProperties()) {
        requestWeatherProperty(propertyName);
    }
}

void Pebble::requestWeatherProperty(const QString &propertyName)
{
    if (propertyName == QString::fromLatin1(WEATHER_LOCATIONS)) {
        refreshWeatherLocations();
    } else {
        requestProperty(propertyName);
    }
}

void Pebble::refreshWeatherLocations()
{
    const quint64 requestEpoch = ++m_weatherLocationsEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QString::fromLatin1(WEATHER_LOCATIONS)), this);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch",
                         QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::weatherLocationsReplyFinished);
}

void Pebble::weatherLocationsReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_weatherLocationsEpoch) {
        return;
    }

    QVariantList locations;
    if (!decodeWeatherLocations(reply, &locations)) {
        qWarning() << "Could not refresh weather locations:" << reply.errorMessage();
        return;
    }
    applyWeatherProperty(QString::fromLatin1(WEATHER_LOCATIONS), locations);
    markWeatherPropertyLoaded(QString::fromLatin1(WEATHER_LOCATIONS));
}

void Pebble::applyWeatherProperty(const QString &propertyName, const QVariant &value)
{
    ++m_weatherValueRevisions[propertyName];
    if (propertyName == QString::fromLatin1(WEATHER_UNITS)) {
        const QString units = value.toString();
        if (m_weatherUnits != units) {
            m_weatherUnits = units;
            emit weatherUnitsChanged();
        }
    } else if (propertyName == QString::fromLatin1(WEATHER_LANGUAGE)) {
        const QString language = value.toString();
        if (m_weatherLanguage != language) {
            m_weatherLanguage = language;
            emit weatherLanguageChanged();
        }
    } else if (propertyName == QString::fromLatin1(WEATHER_ALT_KEY)) {
        const QString altKey = value.toString();
        if (m_weatherAltKey != altKey) {
            m_weatherAltKey = altKey;
            emit weatherAltKeyChanged();
        }
    } else if (propertyName == QString::fromLatin1(WEATHER_LOCATIONS)) {
        const QVariantList locations = value.toList();
        if (m_weatherLocations != locations) {
            m_weatherLocations = locations;
            emit weatherLocationsChanged();
        }
    }
}

void Pebble::markWeatherPropertyLoaded(const QString &propertyName)
{
    m_weatherLoadedProperties.insert(propertyName);
    const QStringList properties = weatherProperties();
    foreach (const QString &weatherProperty, properties) {
        if (!m_weatherLoadedProperties.contains(weatherProperty)) {
            return;
        }
    }
    setWeatherSettingsReady(true);
}

void Pebble::setWeatherSettingsReady(bool ready)
{
    if (m_weatherSettingsReady == ready) {
        return;
    }
    m_weatherSettingsReady = ready;
    emit weatherSettingsReadyChanged();
}

void Pebble::sendWeatherWrite(const QString &propertyName, const QString &method,
                              const QVariant &value, const QVariant &previousValue)
{
    if (propertyName == QString::fromLatin1(WEATHER_LOCATIONS)) {
        ++m_weatherLocationsEpoch;
    } else {
        ++m_propertyEpochs[propertyName];
    }
    const quint64 writeEpoch = ++m_weatherWriteEpochs[propertyName];
    applyWeatherProperty(propertyName, value);
    const quint64 valueRevision = m_weatherValueRevisions.value(propertyName);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(method, QVariantList() << value), this);
    watcher->setProperty("propertyName", propertyName);
    watcher->setProperty("method", method);
    watcher->setProperty("previousValue", previousValue);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    watcher->setProperty("valueRevision",
                         QVariant::fromValue<qulonglong>(valueRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::weatherWriteReplyFinished);
}

void Pebble::weatherWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const QString method = watcher->property("method").toString();
    const QVariant previousValue = watcher->property("previousValue");
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    const quint64 valueRevision = watcher->property("valueRevision").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch ||
            writeEpoch != m_weatherWriteEpochs.value(propertyName)) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << method << "failed:" << reply.errorMessage();
        if (valueRevision == m_weatherValueRevisions.value(propertyName)) {
            applyWeatherProperty(propertyName, previousValue);
        }
    }
    requestWeatherProperty(propertyName);
}

void Pebble::weatherLocationsChangedFromService(const QVariantList &locations)
{
    Q_UNUSED(locations)
    if (!m_weatherRequested) {
        return;
    }
    ++m_weatherValueRevisions[QString::fromLatin1(WEATHER_LOCATIONS)];
    refreshWeatherLocations();
}

QVariantMap Pebble::healthParams() const
{
    return m_healthParams;
}

bool Pebble::healthParamsReady() const
{
    return m_healthParamsReady;
}

QVariantMap Pebble::healthOverview() const
{
    return m_healthOverview;
}

bool Pebble::healthOverviewReady() const
{
    return m_healthOverviewReady;
}

bool Pebble::healthSyncing() const
{
    return m_healthSyncing;
}

void Pebble::refreshHealthParams()
{
    m_healthParamsRequested = true;
    m_healthParamsAuthoritative = false;
    m_healthParamsReadFailures = 0;
    setHealthParamsReady(false);
    requestProperty(QString::fromLatin1(HEALTH_PARAMS));
}

void Pebble::refreshHealthOverview()
{
    m_healthOverviewRequested = true;
    const quint64 requestEpoch = ++m_healthOverviewEpoch;
    setHealthOverviewReady(false);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("HealthOverview")), this);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch",
                         QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::healthOverviewReplyFinished);
}

void Pebble::healthOverviewReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_healthOverviewEpoch) {
        return;
    }

    QVariantMap overview;
    if (!decodeHealthOverview(reply, &overview)) {
        qWarning() << "HealthOverview failed or returned invalid data:"
                   << reply.errorMessage();
        setHealthOverviewReady(true);
        return;
    }
    if (m_healthOverview != overview) {
        m_healthOverview = overview;
        emit healthOverviewChanged();
    }
    setHealthOverviewReady(true);
}

void Pebble::healthDataChangedFromService()
{
    emit healthDataChanged();
    if (m_healthOverviewRequested) {
        refreshHealthOverview();
    }
}

void Pebble::fetchHealthData()
{
    if (m_healthSyncing) {
        return;
    }
    const quint64 syncEpoch = ++m_healthSyncEpoch;
    setHealthSyncing(true);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("FetchHealthData")), this);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("syncEpoch", QVariant::fromValue<qulonglong>(syncEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::healthSyncReplyFinished);
}

void Pebble::healthSyncReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 syncEpoch = watcher->property("syncEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || syncEpoch != m_healthSyncEpoch) {
        return;
    }
    const bool success = reply.type() != QDBusMessage::ErrorMessage;
    if (!success) {
        qWarning() << "FetchHealthData failed:" << reply.errorMessage();
    }
    setHealthSyncing(false);
    emit healthSyncCompleted(success);
    if (success && m_healthOverviewRequested) {
        refreshHealthOverview();
    }
}

void Pebble::setHealthOverviewReady(bool ready)
{
    if (m_healthOverviewReady == ready) {
        return;
    }
    m_healthOverviewReady = ready;
    emit healthOverviewReadyChanged();
}

void Pebble::setHealthSyncing(bool syncing)
{
    if (m_healthSyncing == syncing) {
        return;
    }
    m_healthSyncing = syncing;
    emit healthSyncingChanged();
}

void Pebble::setHealthParams(const QVariantMap &healthParams)
{
    if (healthParams.isEmpty()) {
        qWarning() << "Refusing to save empty health settings";
        return;
    }

    m_healthParamsRequested = true;
    QVariantMap merged = m_healthParams;
    for (QVariantMap::const_iterator it = healthParams.constBegin();
         it != healthParams.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    if (merged == m_healthParams && m_healthParamsAuthoritative) {
        return;
    }

    ++m_propertyEpochs[QString::fromLatin1(HEALTH_PARAMS)];
    const quint64 writeEpoch = ++m_healthParamsWriteEpoch;
    const QVariantMap previousParams = m_healthParams;
    const bool previousValid = m_healthParamsValid;
    m_healthParamsAuthoritative = false;
    m_healthParamsReadFailures = 0;
    setHealthParamsReady(false);
    ++m_healthParamsValueRevision;
    const quint64 valueRevision = m_healthParamsValueRevision;
    m_healthParamsValid = healthParamsSnapshotValid(merged);
    if (m_healthParams != merged) {
        m_healthParams = merged;
        emit healthParamsChanged();
    }

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(
            QStringLiteral("SetHealthParams"),
            QVariantList() << healthParams), this);
    watcher->setProperty("previousParams", previousParams);
    watcher->setProperty("previousValid", previousValid);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    watcher->setProperty("valueRevision",
                         QVariant::fromValue<qulonglong>(valueRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::healthParamsWriteReplyFinished);
}

void Pebble::applyHealthParams(const QVariantMap &params, bool authoritative)
{
    ++m_healthParamsValueRevision;
    m_healthParamsReadFailures = 0;
    m_healthParamsAuthoritative = authoritative;
    m_healthParamsValid = true;
    if (m_healthParams != params) {
        m_healthParams = params;
        emit healthParamsChanged();
    }
    setHealthParamsReady(true);
}

void Pebble::healthParamsReadFailed(quint64 requestEpoch)
{
    if (!m_healthParamsRequested) {
        return;
    }
    ++m_healthParamsReadFailures;
    if (m_healthParamsReadFailures == 1) {
        const quint64 retryServiceEpoch = m_serviceEpoch;
        QTimer::singleShot(250, this,
                           [this, requestEpoch, retryServiceEpoch]() {
            const QString propertyName = QString::fromLatin1(HEALTH_PARAMS);
            if (retryServiceEpoch == m_serviceEpoch
                    && requestEpoch == m_propertyEpochs.value(propertyName)
                    && !m_healthParamsReady) {
                requestProperty(propertyName);
            }
        });
        return;
    }

    m_healthParamsAuthoritative = false;
    if (m_healthParamsValid) {
        setHealthParamsReady(true);
    }
}

void Pebble::setHealthParamsReady(bool ready)
{
    if (m_healthParamsReady == ready) {
        return;
    }
    m_healthParamsReady = ready;
    emit healthParamsReadyChanged();
}

void Pebble::healthParamsWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QVariantMap previousParams = watcher->property("previousParams").toMap();
    const bool previousValid = watcher->property("previousValid").toBool();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    const quint64 valueRevision = watcher->property("valueRevision").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch
            || writeEpoch != m_healthParamsWriteEpoch) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "SetHealthParams failed:" << reply.errorMessage();
        if (valueRevision == m_healthParamsValueRevision) {
            ++m_healthParamsValueRevision;
            m_healthParamsValid = previousValid;
            if (m_healthParams != previousParams) {
                m_healthParams = previousParams;
                emit healthParamsChanged();
            }
        }
    }
    requestProperty(QString::fromLatin1(HEALTH_PARAMS));
}

void Pebble::healthParamsChangedFromService()
{
    if (!m_healthParamsRequested) {
        return;
    }
    ++m_healthParamsValueRevision;
    m_healthParamsAuthoritative = false;
    m_healthParamsReadFailures = 0;
    setHealthParamsReady(false);
    requestProperty(QString::fromLatin1(HEALTH_PARAMS));
}

bool Pebble::imperialUnits() const
{
    return m_imperialUnits;
}

void Pebble::setImperialUnits(bool imperialUnits)
{
    const QString propertyName = QString::fromLatin1(IMPERIAL_UNITS);
    if (imperialUnits == m_imperialUnits
            && m_settingsAuthoritativeProperties.contains(propertyName)) {
        return;
    }
    sendSettingsWrite(propertyName,
                      QStringLiteral("SetImperialUnits"), imperialUnits,
                      m_imperialUnits);
}

QString Pebble::profileWhenConnected() const
{
    return m_profileWhenConnected;
}

QString Pebble::profileWhenDisconnected() const
{
    return m_profileWhenDisconnected;
}

void Pebble::setProfileWhenConnected(const QString &profile)
{
    const QString propertyName = QString::fromLatin1(PROFILE_WHEN_CONNECTED);
    if (profile == m_profileWhenConnected
            && m_settingsAuthoritativeProperties.contains(propertyName)) {
        return;
    }
    sendSettingsWrite(propertyName,
                      QStringLiteral("SetProfileWhenConnected"), profile,
                      m_profileWhenConnected);
}

void Pebble::setProfileWhenDisconnected(const QString &profile)
{
    const QString propertyName = QString::fromLatin1(PROFILE_WHEN_DISCONNECTED);
    if (profile == m_profileWhenDisconnected
            && m_settingsAuthoritativeProperties.contains(propertyName)) {
        return;
    }
    sendSettingsWrite(propertyName,
                      QStringLiteral("SetProfileWhenDisconnected"), profile,
                      m_profileWhenDisconnected);
}

bool Pebble::calendarSyncEnabled() const
{
    return m_calendarSyncEnabled;
}

void Pebble::setCalendarSyncEnabled(bool enabled)
{
    const QString propertyName = QString::fromLatin1(CALENDAR_SYNC_ENABLED);
    if (enabled == m_calendarSyncEnabled
            && m_settingsAuthoritativeProperties.contains(propertyName)) {
        return;
    }
    sendSettingsWrite(propertyName,
                      QStringLiteral("SetCalendarSyncEnabled"), enabled,
                      m_calendarSyncEnabled);
}

bool Pebble::settingsPageReady() const
{
    return m_settingsPageReady;
}

void Pebble::refreshSettingsPage()
{
    m_settingsPageRequested = true;
    m_settingsLoadedProperties.clear();
    m_settingsAuthoritativeProperties.clear();
    m_settingsReadFailures.clear();
    setSettingsPageReady(false);
    foreach (const QString &propertyName, settingsPageProperties()) {
        requestProperty(propertyName);
    }
}

bool Pebble::applySettingsProperty(const QString &propertyName, const QVariant &value)
{
    if ((propertyName == QString::fromLatin1(IMPERIAL_UNITS)
            || propertyName == QString::fromLatin1(CALENDAR_SYNC_ENABLED)
            || propertyName == QString::fromLatin1(SYNC_APPS_FROM_CLOUD))
            && value.type() != QVariant::Bool) {
        return false;
    }
    if ((propertyName == QString::fromLatin1(PROFILE_WHEN_CONNECTED)
            || propertyName == QString::fromLatin1(PROFILE_WHEN_DISCONNECTED))
            && value.type() != QVariant::String) {
        return false;
    }

    ++m_settingsValueRevisions[propertyName];
    if (propertyName == QString::fromLatin1(IMPERIAL_UNITS)) {
        const bool imperial = value.toBool();
        if (m_imperialUnits != imperial) {
            m_imperialUnits = imperial;
            emit imperialUnitsChanged();
        }
    } else if (propertyName == QString::fromLatin1(PROFILE_WHEN_CONNECTED)) {
        const QString profile = value.toString();
        if (m_profileWhenConnected != profile) {
            m_profileWhenConnected = profile;
            emit profileWhenConnectedChanged();
        }
    } else if (propertyName == QString::fromLatin1(PROFILE_WHEN_DISCONNECTED)) {
        const QString profile = value.toString();
        if (m_profileWhenDisconnected != profile) {
            m_profileWhenDisconnected = profile;
            emit profileWhenDisconnectedChanged();
        }
    } else if (propertyName == QString::fromLatin1(CALENDAR_SYNC_ENABLED)) {
        const bool enabled = value.toBool();
        if (m_calendarSyncEnabled != enabled) {
            m_calendarSyncEnabled = enabled;
            emit calendarSyncEnabledChanged();
        }
    } else if (propertyName == QString::fromLatin1(SYNC_APPS_FROM_CLOUD)) {
        const bool enabled = value.toBool();
        if (m_syncAppsFromCloud != enabled) {
            m_syncAppsFromCloud = enabled;
            emit syncAppsFromCloudChanged();
        }
    } else {
        return false;
    }
    return true;
}

void Pebble::markSettingsPropertyLoaded(const QString &propertyName, bool authoritative)
{
    if (!m_settingsPageRequested) {
        return;
    }
    m_settingsReadFailures.remove(propertyName);
    m_settingsLoadedProperties.insert(propertyName);
    if (authoritative) {
        m_settingsAuthoritativeProperties.insert(propertyName);
    } else {
        m_settingsAuthoritativeProperties.remove(propertyName);
    }
    foreach (const QString &settingsProperty, settingsPageProperties()) {
        if (!m_settingsLoadedProperties.contains(settingsProperty)) {
            return;
        }
    }
    setSettingsPageReady(true);
}

void Pebble::settingsPropertyReadFailed(const QString &propertyName,
                                        quint64 requestEpoch)
{
    if (!m_settingsPageRequested) {
        return;
    }
    const int failures = m_settingsReadFailures.value(propertyName) + 1;
    m_settingsReadFailures[propertyName] = failures;
    if (failures == 1) {
        const quint64 retryServiceEpoch = m_serviceEpoch;
        QTimer::singleShot(250, this,
                           [this, propertyName, requestEpoch, retryServiceEpoch]() {
            if (retryServiceEpoch == m_serviceEpoch
                    && requestEpoch == m_propertyEpochs.value(propertyName)
                    && !m_settingsLoadedProperties.contains(propertyName)) {
                requestProperty(propertyName);
            }
        });
        return;
    }

    // Keep the page usable after two failures. The cached value is explicitly
    // non-authoritative, so choosing that same value will still send a write.
    markSettingsPropertyLoaded(propertyName, false);
}

void Pebble::setSettingsPageReady(bool ready)
{
    if (m_settingsPageReady == ready) {
        return;
    }
    m_settingsPageReady = ready;
    emit settingsPageReadyChanged();
}

void Pebble::sendSettingsWrite(const QString &propertyName, const QString &method,
                               const QVariant &value, const QVariant &previousValue)
{
    ++m_propertyEpochs[propertyName];
    const quint64 writeEpoch = ++m_settingsWriteEpochs[propertyName];
    m_settingsLoadedProperties.remove(propertyName);
    m_settingsAuthoritativeProperties.remove(propertyName);
    m_settingsReadFailures.remove(propertyName);
    setSettingsPageReady(false);
    if (!applySettingsProperty(propertyName, value)) {
        return;
    }
    const quint64 valueRevision = m_settingsValueRevisions.value(propertyName);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(method, QVariantList() << value), this);
    watcher->setProperty("propertyName", propertyName);
    watcher->setProperty("method", method);
    watcher->setProperty("previousValue", previousValue);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    watcher->setProperty("valueRevision",
                         QVariant::fromValue<qulonglong>(valueRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::settingsWriteReplyFinished);
}

void Pebble::settingsWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const QString method = watcher->property("method").toString();
    const QVariant previousValue = watcher->property("previousValue");
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    const quint64 valueRevision = watcher->property("valueRevision").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch
            || writeEpoch != m_settingsWriteEpochs.value(propertyName)) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << method << "failed:" << reply.errorMessage();
        if (valueRevision == m_settingsValueRevisions.value(propertyName)) {
            applySettingsProperty(propertyName, previousValue);
        }
    }
    requestProperty(propertyName);
}

void Pebble::settingsPropertyChangedFromService(const QString &propertyName)
{
    if (!m_settingsPageRequested) {
        return;
    }
    ++m_settingsValueRevisions[propertyName];
    m_settingsLoadedProperties.remove(propertyName);
    m_settingsAuthoritativeProperties.remove(propertyName);
    m_settingsReadFailures.remove(propertyName);
    setSettingsPageReady(false);
    requestProperty(propertyName);
}

bool Pebble::devConnEnabled() const
{
    return m_devConnEnabled;
}

void Pebble::setDevConnEnabled(bool enabled)
{
    if (enabled == m_devConnEnabled) {
        return;
    }
    sendDeveloperWrite(QString::fromLatin1(DEV_CONNECTION_ENABLED),
                       QStringLiteral("SetDevConnEnabled"), enabled,
                       m_devConnEnabled,
                       QString::fromLatin1(DEV_CONNECTION_STATE));
}

bool Pebble::devConnServerRunning() const
{
    return m_devConnServerRunning;
}

bool Pebble::developerSettingsReady() const
{
    return m_developerSettingsReady;
}

void Pebble::refreshDeveloperSettings()
{
    m_developerRequested = true;
    m_developerLoadedProperties.clear();
    setDeveloperSettingsReady(false);
    foreach (const QString &propertyName, developerProperties()) {
        requestProperty(propertyName);
    }
}

void Pebble::devConStateChanged(bool state)
{
    qDebug() << "Developer connection state changed:"
             << (state ? "running" : "stopped");
    if (!m_developerRequested) {
        return;
    }
    requestProperty(QString::fromLatin1(DEV_CONNECTION_ENABLED));
    requestProperty(QString::fromLatin1(DEV_CONNECTION_STATE));
}

void Pebble::setLogLevel(int level)
{
    if (level == m_logLevel) {
        return;
    }
    sendDeveloperWrite(QString::fromLatin1(LOG_LEVEL),
                       QStringLiteral("setLogLevel"), level, m_logLevel);
}

int Pebble::getLogLevel() const
{
    return m_logLevel;
}

void Pebble::applyDeveloperProperty(const QString &propertyName, const QVariant &value)
{
    ++m_developerValueRevisions[propertyName];
    if (propertyName == QString::fromLatin1(DEV_CONNECTION_ENABLED)) {
        const bool enabled = value.toBool();
        if (m_devConnEnabled != enabled) {
            m_devConnEnabled = enabled;
            emit devConnEnabledChanged();
        }
    } else if (propertyName == QString::fromLatin1(DEV_CONNECTION_STATE)) {
        const bool running = value.toBool();
        if (m_devConnServerRunning != running) {
            m_devConnServerRunning = running;
            emit devConnServerRunningChanged();
        }
    } else if (propertyName == QString::fromLatin1(LOG_LEVEL)) {
        const int level = value.toInt();
        if (m_logLevel != level) {
            m_logLevel = level;
            emit logLevelChanged();
        }
    }
}

void Pebble::markDeveloperPropertyLoaded(const QString &propertyName)
{
    m_developerLoadedProperties.insert(propertyName);
    foreach (const QString &developerProperty, developerProperties()) {
        if (!m_developerLoadedProperties.contains(developerProperty)) {
            return;
        }
    }
    setDeveloperSettingsReady(true);
}

void Pebble::setDeveloperSettingsReady(bool ready)
{
    if (m_developerSettingsReady == ready) {
        return;
    }
    m_developerSettingsReady = ready;
    emit developerSettingsReadyChanged();
}

void Pebble::sendDeveloperWrite(const QString &propertyName, const QString &method,
                                const QVariant &value, const QVariant &previousValue,
                                const QString &refreshProperty)
{
    ++m_propertyEpochs[propertyName];
    const quint64 writeEpoch = ++m_developerWriteEpochs[propertyName];
    applyDeveloperProperty(propertyName, value);
    const quint64 valueRevision = m_developerValueRevisions.value(propertyName);

    if (!refreshProperty.isEmpty()) {
        ++m_propertyEpochs[refreshProperty];
    }

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(method, QVariantList() << value), this);
    watcher->setProperty("propertyName", propertyName);
    watcher->setProperty("method", method);
    watcher->setProperty("previousValue", previousValue);
    watcher->setProperty("refreshProperty", refreshProperty);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    watcher->setProperty("valueRevision",
                         QVariant::fromValue<qulonglong>(valueRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::developerWriteReplyFinished);
}

void Pebble::developerWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const QString method = watcher->property("method").toString();
    const QVariant previousValue = watcher->property("previousValue");
    const QString refreshProperty = watcher->property("refreshProperty").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    const quint64 valueRevision = watcher->property("valueRevision").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch ||
            writeEpoch != m_developerWriteEpochs.value(propertyName)) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << method << "failed:" << reply.errorMessage();
        if (valueRevision == m_developerValueRevisions.value(propertyName)) {
            applyDeveloperProperty(propertyName, previousValue);
        }
    }

    requestProperty(propertyName);
    if (!refreshProperty.isEmpty()) {
        requestProperty(refreshProperty);
    }
}

bool Pebble::accountAuthenticated() const
{
    return m_account->authenticated();
}

void Pebble::setOAuthToken(const QString &token)
{
    m_account->setOAuthToken(token);
}

bool Pebble::setOAuthTokenFromCallback(const QString &callbackUrl)
{
    const QString token =
        RockpoolAccount::oauthTokenFromCallback(callbackUrl);
    if (token.isEmpty()) {
        return false;
    }
    m_account->setOAuthToken(token);
    return true;
}

QString Pebble::accountName() const
{
    return m_account->name();
}

QString Pebble::accountEmail() const
{
    return m_account->email();
}

bool Pebble::accountTokenPending() const
{
    return m_account->tokenPending();
}

QString Pebble::accountTokenError() const
{
    return m_account->tokenError();
}

bool Pebble::syncAppsFromCloud() const
{
    return m_syncAppsFromCloud;
}
void Pebble::setSyncAppsFromCloud(bool enable)
{
    const QString propertyName = QString::fromLatin1(SYNC_APPS_FROM_CLOUD);
    if (enable == m_syncAppsFromCloud
            && m_settingsAuthoritativeProperties.contains(propertyName)) {
        return;
    }
    sendSettingsWrite(propertyName,
                      QStringLiteral("setSyncAppsFromCloud"), enable,
                      m_syncAppsFromCloud);
}

void Pebble::resetTimeline()
{
    // The compatibility method only queues the daemon-side reset.  Its void
    // reply is transport acknowledgement, not confirmation that the watch
    // databases were cleared, so do not synthesize a completed UI state.
    sendVoidCommand(QStringLiteral("resetTimeline"));
}

int Pebble::timelineWindowStart() const
{
    return m_timelienWindowStart;
}

int Pebble::timelineWindowFade() const
{
    return m_timelienWindowFade;
}

int Pebble::timelineWindowEnd() const
{
    return m_timelienWindowEnd;
}

bool Pebble::timelineWindowReady() const
{
    return m_timelineWindowReady;
}

void Pebble::setTimelineWindowReady(bool ready)
{
    if (m_timelineWindowReady != ready) {
        m_timelineWindowReady = ready;
        emit timelineWindowReadyChanged();
    }
}

void Pebble::refreshQuietTime()
{
    m_quietTimeRequested = true;
    if (m_quietTimeBusy) {
        m_quietTimeRefreshPending = true;
        return;
    }
    m_quietTimeBusy = true;
    emit quietTimeChanged();
    const quint64 serviceEpoch = m_serviceEpoch;
    auto *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("QuietTimeSettings")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, serviceEpoch](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QVariantMap> reply = *finished;
        finished->deleteLater();
        if (serviceEpoch != m_serviceEpoch) return;
        m_quietTimeBusy = false;
        if (reply.isError()) {
            m_quietTimeReady = false;
            m_quietTimeError = tr("Could not load Quiet Time settings.");
        } else {
            if (!m_quietTimeReady) m_quietTimeError.clear();
            m_quietTimeSettings = reply.value();
            m_quietTimeReady = true;
        }
        emit quietTimeChanged();
        if (m_quietTimeRefreshPending) {
            m_quietTimeRefreshPending = false;
            refreshQuietTime();
        }
    });
}

void Pebble::setQuietTimeSetting(const QString &key, const QString &value)
{
    if (!m_quietTimeReady || m_quietTimeBusy) return;
    m_quietTimeBusy = true;
    m_quietTimeError.clear();
    emit quietTimeChanged();
    const quint64 serviceEpoch = m_serviceEpoch;
    auto *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("SetQuietTimeSetting"), key, value), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, serviceEpoch](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<bool> reply = *finished;
        finished->deleteLater();
        if (serviceEpoch != m_serviceEpoch) return;
        m_quietTimeBusy = false;
        if (reply.isError() || !reply.value()) {
            m_quietTimeError = tr("Could not save Quiet Time settings.");
        }
        m_quietTimeRefreshPending = false;
        refreshQuietTime();
    });
}

void Pebble::refreshTimelineWindow()
{
    if (m_timelineWindowWriteInFlight) {
        return;
    }
    m_timelineWindowReadFailures = 0;
    requestTimelineWindowSnapshot();
}

void Pebble::requestTimelineWindowSnapshot()
{
    const quint64 requestEpoch = ++m_timelineWindowRequestEpoch;
    m_pendingTimelineWindowValues.clear();
    m_pendingTimelineWindowReplies.clear();
    m_timelineWindowRequestFailed = false;
    setTimelineWindowReady(false);

    foreach (const QString &propertyName, timelineWindowProperties()) {
        m_pendingTimelineWindowReplies.insert(propertyName);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            m_iface->asyncCall(propertyName), this);
        watcher->setProperty("propertyName", propertyName);
        watcher->setProperty("requestEpoch",
                             QVariant::fromValue<qulonglong>(requestEpoch));
        watcher->setProperty("serviceEpoch",
                             QVariant::fromValue<qulonglong>(m_serviceEpoch));
        connect(watcher, &QDBusPendingCallWatcher::finished,
                this, &Pebble::timelineWindowPropertyReplyFinished);
    }
}

void Pebble::timelineWindowPropertyReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_timelineWindowRequestEpoch) {
        return;
    }

    m_pendingTimelineWindowReplies.remove(propertyName);
    bool valid = reply.type() != QDBusMessage::ErrorMessage
            && reply.arguments().count() == 1;
    int value = 0;
    if (valid) {
        bool converted = false;
        value = reply.arguments().first().toInt(&converted);
        valid = converted;
    }
    if (valid) {
        m_pendingTimelineWindowValues.insert(propertyName, value);
    } else {
        m_timelineWindowRequestFailed = true;
        qWarning() << "Could not refresh timeline window property" << propertyName
                   << reply.errorMessage();
    }

    if (!m_pendingTimelineWindowReplies.isEmpty()) {
        return;
    }
    if (m_timelineWindowRequestFailed
            || m_pendingTimelineWindowValues.count() != timelineWindowProperties().count()) {
        timelineWindowSnapshotFailed(requestEpoch, serviceEpoch);
        return;
    }

    const int rawStart = m_pendingTimelineWindowValues.value(
        QStringLiteral("timelineWindowStart")).toInt();
    const int rawFade = m_pendingTimelineWindowValues.value(
        QStringLiteral("timelineWindowFade")).toInt();
    const int end = m_pendingTimelineWindowValues.value(
        QStringLiteral("timelineWindowEnd")).toInt();
    if (rawStart > -1 || rawStart < -365 || rawFade > 0 || rawFade < -2592000
            || end < -365 || end > 365 || rawStart > end) {
        qWarning() << "Ignoring invalid timeline window snapshot"
                   << rawStart << rawFade << end;
        timelineWindowSnapshotFailed(requestEpoch, serviceEpoch);
        return;
    }

    const int start = -rawStart;
    const int fade = -rawFade;
    const bool changed = m_timelienWindowStart != start
            || m_timelienWindowFade != fade || m_timelienWindowEnd != end;
    m_timelienWindowStart = start;
    m_timelienWindowFade = fade;
    m_timelienWindowEnd = end;
    if (changed) {
        emit timelineWindowChanged();
    }
    m_timelineWindowHasSnapshot = true;
    m_timelineWindowReadFailures = 0;
    setTimelineWindowReady(true);
}

void Pebble::timelineWindowSnapshotFailed(quint64 requestEpoch,
                                          quint64 serviceEpoch)
{
    ++m_timelineWindowReadFailures;
    if (m_timelineWindowReadFailures < 2) {
        QTimer::singleShot(250, this, [this, requestEpoch, serviceEpoch]() {
            if (requestEpoch == m_timelineWindowRequestEpoch
                    && serviceEpoch == m_serviceEpoch
                    && !m_timelineWindowWriteInFlight
                    && !m_timelineWindowReady) {
                requestTimelineWindowSnapshot();
            }
        });
    } else if (m_timelineWindowHasSnapshot) {
        // Keep a previously validated tuple usable after a bounded transient
        // read failure.  A cold/default cache never becomes writable.
        setTimelineWindowReady(true);
    }
}

void Pebble::setTimelineWindow(int start, int fade, int end)
{
    if (start < 1 || start > 365 || fade < 0 || fade > 2592000
            || end < -365 || end > 365 || -start > end) {
        qWarning() << "Ignoring invalid timeline window" << start << fade << end;
        refreshTimelineWindow();
        return;
    }

    const quint64 writeEpoch = ++m_timelineWindowWriteEpoch;
    ++m_timelineWindowRequestEpoch;
    m_pendingTimelineWindowValues.clear();
    m_pendingTimelineWindowReplies.clear();
    m_timelineWindowRequestFailed = false;
    m_timelineWindowReadFailures = 0;
    setTimelineWindowReady(false);

    if (m_timelineWindowWriteInFlight) {
        m_timelineWindowWriteQueued = true;
        m_queuedTimelineWindowStart = start;
        m_queuedTimelineWindowFade = fade;
        m_queuedTimelineWindowEnd = end;
        m_queuedTimelineWindowWriteEpoch = writeEpoch;
        return;
    }
    dispatchTimelineWindowWrite(start, fade, end, writeEpoch);
}

void Pebble::dispatchTimelineWindowWrite(int start, int fade, int end,
                                         quint64 writeEpoch)
{
    m_timelineWindowWriteInFlight = true;
    m_inFlightTimelineWindowWriteEpoch = writeEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(
            QStringLiteral("setTimelineWindow"),
            QVariantList() << -start << -fade << end), this);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("writeEpoch", QVariant::fromValue<qulonglong>(writeEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::timelineWindowWriteReplyFinished);
}

void Pebble::timelineWindowWriteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 writeEpoch = watcher->property("writeEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || !m_timelineWindowWriteInFlight
            || writeEpoch != m_inFlightTimelineWindowWriteEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "setTimelineWindow failed:" << reply.errorMessage();
    }

    if (m_timelineWindowWriteQueued) {
        const int start = m_queuedTimelineWindowStart;
        const int fade = m_queuedTimelineWindowFade;
        const int end = m_queuedTimelineWindowEnd;
        const quint64 queuedEpoch = m_queuedTimelineWindowWriteEpoch;
        m_timelineWindowWriteQueued = false;
        dispatchTimelineWindowWrite(start, fade, end, queuedEpoch);
        return;
    }

    m_timelineWindowWriteInFlight = false;
    m_timelineWindowReadFailures = 0;
    requestTimelineWindowSnapshot();
}

void Pebble::configurationClosed(const QString &uuid, const QString &url)
{
    sendVoidCommand(QStringLiteral("ConfigurationClosed"),
                    QVariantList() << uuid << url);
}

void Pebble::launchApp(const QString &uuid)
{
    sendVoidCommand(QStringLiteral("LaunchApp"), QVariantList() << uuid);
}

void Pebble::requestConfigurationURL(const QString &uuid)
{
    sendVoidCommand(QStringLiteral("ConfigurationURL"), QVariantList() << uuid);
}

void Pebble::removeApp(const QString &uuid)
{
    qDebug() << "should remove app" << uuid;
    sendVoidCommand(QStringLiteral("RemoveApp"), QVariantList() << uuid);
}

void Pebble::installApp(const QString &storeId)
{
    qDebug() << "should install app" << storeId;
    sendVoidCommand(QStringLiteral("InstallApp"), QVariantList() << storeId);
}

void Pebble::sideloadApp(const QString &packageFile)
{
    sendVoidCommand(QStringLiteral("SideloadApp"), QVariantList() << packageFile);
}

void Pebble::sendVoidCommand(const QString &method, const QVariantList &arguments)
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(method, arguments), this);
    watcher->setProperty("method", method);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::voidCommandReplyFinished);
}

void Pebble::voidCommandReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString method = watcher->property("method").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << method << "failed:" << reply.errorMessage();
    }
}

void Pebble::requestProperty(const QString &propertyName)
{
    const quint64 requestEpoch = ++m_propertyEpochs[propertyName];
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(propertyName), this);
    watcher->setProperty("propertyName", propertyName);
    watcher->setProperty("requestEpoch", QVariant::fromValue<qulonglong>(requestEpoch));
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::propertyReplyFinished);
}

void Pebble::scheduleAddressRetry(quint64 requestEpoch, quint64 serviceEpoch)
{
    if (serviceEpoch != m_serviceEpoch ||
            requestEpoch != m_propertyEpochs.value(QStringLiteral("Address")) ||
            !m_address.isEmpty()) {
        return;
    }
    const int shift = qMin(m_addressReadFailures, 7);
    ++m_addressReadFailures;
    const int delayMs = 250 * (1 << shift);
    QTimer::singleShot(delayMs, this, [this, requestEpoch, serviceEpoch]() {
        if (serviceEpoch == m_serviceEpoch &&
                requestEpoch == m_propertyEpochs.value(QStringLiteral("Address")) &&
                m_address.isEmpty()) {
            requestProperty(QStringLiteral("Address"));
        }
    });
}

void Pebble::propertyReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch ||
            requestEpoch != m_propertyEpochs.value(propertyName)) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() != 1) {
        qWarning() << "Could not refresh" << propertyName << reply.errorMessage();
        if (settingsPageProperties().contains(propertyName)) {
            settingsPropertyReadFailed(propertyName, requestEpoch);
        } else if (propertyName == QString::fromLatin1(HEALTH_PARAMS)) {
            healthParamsReadFailed(requestEpoch);
        } else if (propertyName == QString::fromLatin1(CANNED_RESPONSES)) {
            cannedResponsesReadFailed(requestEpoch);
        } else if (propertyName == QStringLiteral("Address")) {
            scheduleAddressRetry(requestEpoch, serviceEpoch);
        }
        return;
    }
    applyProperty(propertyName, reply.arguments().first());
}

void Pebble::applyProperty(const QString &propertyName, const QVariant &value)
{
    if (propertyName == QString::fromLatin1(HEALTH_PARAMS)) {
        QVariantMap params;
        if (decodeHealthParams(value, &params)) {
            applyHealthParams(params, true);
        } else {
            qWarning() << "Invalid health settings" << value;
            healthParamsReadFailed(m_propertyEpochs.value(propertyName));
        }
    } else if (propertyName == QString::fromLatin1(CANNED_RESPONSES)) {
        QVariantMap responses;
        if (decodeStringMap(value, &responses)) {
            applyCannedResponses(responses, true);
        } else {
            qWarning() << "Invalid canned responses" << value;
            cannedResponsesReadFailed(m_propertyEpochs.value(propertyName));
        }
    } else if (propertyName == QString::fromLatin1(IMPERIAL_UNITS) ||
            propertyName == QString::fromLatin1(PROFILE_WHEN_CONNECTED) ||
            propertyName == QString::fromLatin1(PROFILE_WHEN_DISCONNECTED) ||
            propertyName == QString::fromLatin1(CALENDAR_SYNC_ENABLED) ||
            propertyName == QString::fromLatin1(SYNC_APPS_FROM_CLOUD)) {
        if (applySettingsProperty(propertyName, value)) {
            markSettingsPropertyLoaded(propertyName, true);
        } else {
            qWarning() << "Invalid settings value for" << propertyName << value;
            settingsPropertyReadFailed(
                propertyName, m_propertyEpochs.value(propertyName));
        }
    } else if (propertyName == QString::fromLatin1(WEATHER_UNITS) ||
            propertyName == QString::fromLatin1(WEATHER_LANGUAGE) ||
            propertyName == QString::fromLatin1(WEATHER_ALT_KEY)) {
        applyWeatherProperty(propertyName, value);
        markWeatherPropertyLoaded(propertyName);
    } else if (propertyName == QString::fromLatin1(DEV_CONNECTION_ENABLED) ||
            propertyName == QString::fromLatin1(DEV_CONNECTION_STATE) ||
            propertyName == QString::fromLatin1(LOG_LEVEL)) {
        applyDeveloperProperty(propertyName, value);
        markDeveloperPropertyLoaded(propertyName);
    } else if (propertyName == QStringLiteral("Name")) {
        const QString name = value.toString();
        if (m_name != name) {
            m_name = name;
            emit identityChanged();
        }
    } else if (propertyName == QStringLiteral("Address")) {
        if (value.type() != QVariant::String || value.toString().isEmpty()) {
            qWarning() << "Invalid watch address" << value;
            scheduleAddressRetry(m_propertyEpochs.value(propertyName), m_serviceEpoch);
            return;
        }
        const QString address = value.toString();
        m_addressReadFailures = 0;
        if (m_address != address) {
            m_address = address;
            emit identityChanged();
        }
    } else if (propertyName == QStringLiteral("SerialNumber")) {
        const QString serialNumber = value.toString();
        if (m_serialNumber != serialNumber) {
            m_serialNumber = serialNumber;
            emit identityChanged();
        }
    } else if (propertyName == QStringLiteral("PlatformString")) {
        const QString platformString = value.toString();
        if (m_platformString != platformString) {
            m_platformString = platformString;
            emit platformStringChanged();
        }
    } else if (propertyName == QStringLiteral("HardwarePlatform")) {
        const QString hardwarePlatform = value.toString();
        if (m_hardwarePlatform != hardwarePlatform) {
            m_hardwarePlatform = hardwarePlatform;
            emit hardwarePlatformChanged();
        }
    } else if (propertyName == QStringLiteral("SoftwareVersion")) {
        const QString softwareVersion = value.toString();
        if (m_softwareVersion != softwareVersion) {
            m_softwareVersion = softwareVersion;
            emit softwareVersionChanged();
        }
    } else if (propertyName == QStringLiteral("LanguageVersion")) {
        const QString languageVersion = value.toString();
        if (m_languageVersion != languageVersion) {
            m_languageVersion = languageVersion;
            emit languageVersionChanged();
        }
    } else if (propertyName == QStringLiteral("Model")) {
        const int model = value.toInt();
        if (m_model != model) {
            m_model = model;
            emit modelChanged();
        }
    } else if (propertyName == QStringLiteral("Recovery")) {
        const bool recovery = value.toBool();
        if (m_recovery != recovery) {
            m_recovery = recovery;
            emit recoveryChanged();
        }
    } else if (propertyName == QStringLiteral("IsConnected")) {
        const bool connected = value.toBool();
        if (m_connected != connected) {
            m_connected = connected;
            emit connectedChanged();
        }
    }
}

void Pebble::dataChanged()
{
    qDebug() << "data changed";
    foreach (const QString &propertyName, bootstrapProperties()) {
        requestProperty(propertyName);
    }
    refreshTimelineWindow();
    refreshConnectionState();
}

void Pebble::refreshLanguageVersion()
{
    requestProperty(QStringLiteral("LanguageVersion"));
}

void Pebble::refreshConnectionState()
{
    const quint64 epoch = ++m_connectionEpoch;
    m_pendingConnectionValues.clear();
    m_pendingConnectionReplies.clear();
    const QStringList properties = QStringList()
        << QStringLiteral("ConnectionState")
        << QStringLiteral("LastError");
    foreach (const QString &propertyName, properties) {
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            m_iface->asyncCall(propertyName), this);
        watcher->setProperty("propertyName", propertyName);
        watcher->setProperty("connectionEpoch", QVariant::fromValue<qulonglong>(epoch));
        watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
        connect(watcher, &QDBusPendingCallWatcher::finished,
                this, &Pebble::connectionPropertyReplyFinished);
    }
}

void Pebble::connectionPropertyReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const quint64 epoch = watcher->property("connectionEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || epoch != m_connectionEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() != 1) {
        qWarning() << "Could not refresh" << propertyName << reply.errorMessage();
        return;
    }

    m_pendingConnectionValues.insert(propertyName, reply.arguments().first());
    m_pendingConnectionReplies.insert(propertyName);
    if (!m_pendingConnectionReplies.contains(QStringLiteral("ConnectionState")) ||
            !m_pendingConnectionReplies.contains(QStringLiteral("LastError"))) {
        return;
    }

    const int state = m_pendingConnectionValues.value(QStringLiteral("ConnectionState")).toInt();
    const QString error = m_pendingConnectionValues.value(QStringLiteral("LastError")).toString();
    if (m_connectionState != state || m_lastError != error) {
        m_connectionState = state;
        m_lastError = error;
        emit connectionStateChanged();
    }
}

void Pebble::pebbleConnected()
{
    dataChanged();
    if (!m_connected) {
        m_connected = true;
        emit connectedChanged();
    }

    refreshApps();
    refreshNotifications();
    refreshScreenshots();
}

void Pebble::pebbleDisconnected()
{
    ++m_propertyEpochs[QStringLiteral("IsConnected")];
    ++m_connectionEpoch;
    m_pendingConnectionValues.clear();
    m_pendingConnectionReplies.clear();
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    refreshConnectionState();
}

int Pebble::connectionState() const
{
    return m_connectionState;
}

QString Pebble::lastError() const
{
    return m_lastError;
}

void Pebble::pebbleConnectionStateChanged(int state)
{
    const quint64 epoch = ++m_connectionEpoch;
    m_pendingConnectionValues.clear();
    m_pendingConnectionReplies.clear();
    m_pendingConnectionValues.insert(QStringLiteral("ConnectionState"), state);
    m_pendingConnectionReplies.insert(QStringLiteral("ConnectionState"));

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("LastError")), this);
    watcher->setProperty("propertyName", QStringLiteral("LastError"));
    watcher->setProperty("connectionEpoch", QVariant::fromValue<qulonglong>(epoch));
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::connectionPropertyReplyFinished);
}

void Pebble::notificationFilterChanged(const QString &sourceId, const QString &name, const QString &icon, const int enabled)
{
    ++m_notificationFiltersEpoch;
    m_notifications->insert(sourceId, name, icon, enabled);

    QVariantMap filters = m_notificationFilters;
    if (enabled < 0) {
        filters.remove(sourceId);
    } else {
        QVariantMap entry = filters.value(sourceId).toMap();
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("icon"), icon);
        entry.insert(QStringLiteral("enabled"), enabled);
        filters.insert(sourceId, entry);
    }
    if (m_notificationFilters != filters) {
        m_notificationFilters = filters;
        emit notificationsFilterChanged();
    }
    refreshNotificationsAsync();
}

QVariantMap Pebble::notificationsFilter() const
{
    return m_notificationFilters;
}

void Pebble::refreshNotifications()
{
    refreshNotificationsAsync();
}

void Pebble::refreshNotificationsAsync()
{
    const quint64 epoch = ++m_notificationFiltersEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("NotificationsFilter")), this);
    watcher->setProperty("notificationFiltersEpoch", QVariant::fromValue<qulonglong>(epoch));
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::notificationFiltersReplyFinished);
}

void Pebble::notificationFiltersReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    const quint64 epoch = watcher->property("notificationFiltersEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || epoch != m_notificationFiltersEpoch) {
        return;
    }
    if (reply.isError()) {
        qWarning() << "Could not refresh notification filters:" << reply.error().message();
        return;
    }
    applyNotificationFilters(reply.value());
}

void Pebble::applyNotificationFilters(const QVariantMap &filters)
{
    QVariantMap parsedFilters;
    foreach (const QString &sourceId, filters.keys()) {
        QVariantMap notifEntry;
        if (!decodeVariantMap(filters.value(sourceId), &notifEntry)) {
            qWarning() << "Could not decode notification filter" << sourceId;
            return;
        }
        parsedFilters.insert(sourceId, notifEntry);
    }

    foreach (const QString &sourceId, m_notificationFilters.keys()) {
        if (!parsedFilters.contains(sourceId)) {
            m_notifications->insert(sourceId, QString(), QString(), -1);
        }
    }
    foreach (const QString &sourceId, parsedFilters.keys()) {
        const QVariantMap notifEntry = parsedFilters.value(sourceId).toMap();
        m_notifications->insert(sourceId, notifEntry.value("name").toString(), notifEntry.value("icon").toString(), notifEntry.value("enabled").toInt());
        m_notifications->setAppearance(sourceId, notifEntry.value("colorName").toString(), notifEntry.value("iconCode").toString());
    }

    if (m_notificationFilters != parsedFilters) {
        m_notificationFilters = parsedFilters;
        emit notificationsFilterChanged();
    }
}

void Pebble::setNotificationFilter(const QString &sourceId, int enabled)
{
    sendNotificationFilterCommand(
        QStringLiteral("SetNotificationFilter"), sourceId,
        QVariantList() << sourceId << enabled);
}

void Pebble::forgetNotificationFilter(const QString &sourceId)
{
    sendNotificationFilterCommand(
        QStringLiteral("ForgetNotificationFilter"), sourceId,
        QVariantList() << sourceId);
}

void Pebble::sendNotificationFilterCommand(const QString &method,
                                           const QString &sourceId,
                                           const QVariantList &arguments)
{
    const quint64 commandEpoch = ++m_notificationFilterCommandEpochs[sourceId];
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(method, arguments), this);
    watcher->setProperty("method", method);
    watcher->setProperty("sourceId", sourceId);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("commandEpoch",
                         QVariant::fromValue<qulonglong>(commandEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::notificationFilterCommandReplyFinished);
}

void Pebble::notificationFilterCommandReplyFinished(
    QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString method = watcher->property("method").toString();
    const QString sourceId = watcher->property("sourceId").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 commandEpoch = watcher->property("commandEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch
            || commandEpoch != m_notificationFilterCommandEpochs.value(sourceId)) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << method << "failed for" << sourceId << reply.errorMessage();
        refreshNotificationsAsync();
    }
}

void Pebble::setNotificationAppColor(const QString &sourceId, const QString &colorName)
{
    ++m_notificationFiltersEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("SetNotificationAppColor"), sourceId, colorName), this);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::notificationAppearanceReplyFinished);
    m_notifications->setColorName(sourceId, colorName);
}

void Pebble::setNotificationAppIcon(const QString &sourceId, const QString &iconCode)
{
    ++m_notificationFiltersEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("SetNotificationAppIcon"), sourceId, iconCode), this);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::notificationAppearanceReplyFinished);
    m_notifications->setIconCode(sourceId, iconCode);
}

void Pebble::notificationAppearanceReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<> reply = *watcher;
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch) {
        return;
    }
    if (reply.isError()) {
        qWarning() << "Could not update notification appearance:" << reply.error().message();
    }
    // The compatibility method starts persistence asynchronously. Re-read even after a successful
    // method reply so a rejected/unknown update rolls back the optimistic model value; a later
    // NotificationFilterChanged signal converges a delayed successful write.
    refreshNotificationsAsync();
}

QVariantList Pebble::timelineColors() const
{
    return m_timelineColors;
}

QVariantList Pebble::timelineIcons() const
{
    return m_timelineIcons;
}

bool Pebble::timelineColorsReady() const
{
    return m_timelineColorsReady;
}

bool Pebble::timelineIconsReady() const
{
    return m_timelineIconsReady;
}

void Pebble::refreshTimelineColors()
{
    m_timelineColorsRequested = true;
    m_timelineColorsFailures = 0;
    setTimelinePaletteReady(QString::fromLatin1(TIMELINE_COLORS), false);
    refreshTimelinePalette(QString::fromLatin1(TIMELINE_COLORS));
}

void Pebble::refreshTimelineIcons()
{
    m_timelineIconsRequested = true;
    m_timelineIconsFailures = 0;
    setTimelinePaletteReady(QString::fromLatin1(TIMELINE_ICONS), false);
    refreshTimelinePalette(QString::fromLatin1(TIMELINE_ICONS));
}

void Pebble::refreshTimelinePalette(const QString &method)
{
    bool *inFlight = 0;
    quint64 *epoch = 0;
    if (method == QString::fromLatin1(TIMELINE_COLORS)) {
        inFlight = &m_timelineColorsInFlight;
        epoch = &m_timelineColorsEpoch;
    } else if (method == QString::fromLatin1(TIMELINE_ICONS)) {
        inFlight = &m_timelineIconsInFlight;
        epoch = &m_timelineIconsEpoch;
    } else {
        return;
    }
    if (*inFlight) {
        return;
    }

    *inFlight = true;
    const quint64 requestEpoch = ++(*epoch);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(method), this);
    watcher->setProperty("method", method);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch",
                         QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::timelinePaletteReplyFinished);
}

void Pebble::timelinePaletteReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString method = watcher->property("method").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();

    bool *inFlight = 0;
    int *failures = 0;
    quint64 currentEpoch = 0;
    if (method == QString::fromLatin1(TIMELINE_COLORS)) {
        inFlight = &m_timelineColorsInFlight;
        failures = &m_timelineColorsFailures;
        currentEpoch = m_timelineColorsEpoch;
    } else if (method == QString::fromLatin1(TIMELINE_ICONS)) {
        inFlight = &m_timelineIconsInFlight;
        failures = &m_timelineIconsFailures;
        currentEpoch = m_timelineIconsEpoch;
    } else {
        return;
    }
    if (serviceEpoch != m_serviceEpoch || requestEpoch != currentEpoch) {
        return;
    }
    *inFlight = false;

    QVariantList values;
    if (!decodeVariantMapList(reply, &values)
            || !validateTimelinePalette(method, values)) {
        qWarning() << "Could not refresh" << method << reply.errorMessage();
        ++(*failures);
        if (*failures == 1) {
            const quint64 retryServiceEpoch = m_serviceEpoch;
            QTimer::singleShot(250, this, [this, method, retryServiceEpoch]() {
                if (retryServiceEpoch == m_serviceEpoch) {
                    refreshTimelinePalette(method);
                }
            });
        } else {
            // A completed empty snapshot is preferable to an indefinitely spinning picker. A
            // later page visit explicitly refreshes again, while any last-good cache is retained.
            setTimelinePaletteReady(method, true);
        }
        return;
    }

    *failures = 0;

    if (method == QString::fromLatin1(TIMELINE_COLORS)) {
        if (m_timelineColors != values) {
            m_timelineColors = values;
            emit timelineColorsChanged();
        }
    } else if (m_timelineIcons != values) {
        m_timelineIcons = values;
        emit timelineIconsChanged();
    }
    setTimelinePaletteReady(method, true);
}

void Pebble::setTimelinePaletteReady(const QString &method, bool ready)
{
    if (method == QString::fromLatin1(TIMELINE_COLORS)) {
        if (m_timelineColorsReady == ready) {
            return;
        }
        m_timelineColorsReady = ready;
        emit timelineColorsReadyChanged();
    } else if (method == QString::fromLatin1(TIMELINE_ICONS)) {
        if (m_timelineIconsReady == ready) {
            return;
        }
        m_timelineIconsReady = ready;
        emit timelineIconsReadyChanged();
    }
}

void Pebble::refreshApps()
{
    const quint64 epoch = ++m_appsEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("InstalledApps")), this);
    watcher->setProperty("appsEpoch", QVariant::fromValue<qulonglong>(epoch));
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::appsReplyFinished);
}

void Pebble::appsReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 epoch = watcher->property("appsEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || epoch != m_appsEpoch) {
        return;
    }

    QVariantList appList;
    if (!decodeVariantMapList(reply, &appList)) {
        qWarning() << "Could not fetch installed apps" << reply.errorMessage();
        return;
    }

    QList<AppItem*> applications;
    QList<AppItem*> watchfaces;
    foreach (const QVariant &v, appList) {
        AppItem *app = new AppItem();
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
            watchfaces.append(app);
        } else if (app->uuid() == QStringLiteral("{07e0d9cb-8957-4bf7-9d42-35bf47caadfe}")) {
            // The movement controls and SetAppOrder require Settings to be first,
            // even when the locker supplies a different saved order.
            applications.prepend(app);
        } else {
            applications.append(app);
        }
    }

    qDebug() << "have apps" << appList;
    m_installedApps->clear();
    m_installedWatchfaces->clear();
    foreach (AppItem *app, applications) {
        m_installedApps->insert(app);
    }
    foreach (AppItem *watchface, watchfaces) {
        m_installedWatchfaces->insert(watchface);
    }
}

void Pebble::appsSorted()
{
    ++m_appsEpoch;
    QStringList newList;
    for (int i = 0; i < m_installedApps->rowCount(); i++) {
        newList << m_installedApps->get(i)->uuid();
    }
    for (int i = 0; i < m_installedWatchfaces->rowCount(); i++) {
        newList << m_installedWatchfaces->get(i)->uuid();
    }
    sendVoidCommand(QStringLiteral("SetAppOrder"), QVariantList() << newList);
}

void Pebble::refreshScreenshots()
{
    const quint64 epoch = ++m_screenshotsEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("Screenshots")), this);
    watcher->setProperty("screenshotsEpoch", QVariant::fromValue<qulonglong>(epoch));
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::screenshotsReplyFinished);
}

void Pebble::screenshotsReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 epoch = watcher->property("screenshotsEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || epoch != m_screenshotsEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() != 1) {
        qWarning() << "Could not refresh screenshots" << reply.errorMessage();
        return;
    }

    QStringList screenshots;
    if (!decodeStringList(reply.arguments().first(), &screenshots)) {
        qWarning() << "Could not decode screenshots";
        return;
    }
    m_screenshotModel->clear();
    foreach (const QString &filename, screenshots) {
        m_screenshotModel->insert(filename);
    }
}

void Pebble::screenshotAdded(const QString &filename)
{
    qDebug() << "screenshot added" << filename;
    ++m_screenshotsEpoch;
    m_screenshotModel->insert(filename);
    refreshScreenshots();
}

void Pebble::screenshotRemoved(const QString &filename)
{
    ++m_screenshotsEpoch;
    m_screenshotModel->remove(filename);
    refreshScreenshots();
}

void Pebble::refreshFirmwareUpdateInfo()
{
    const quint64 epoch = ++m_firmwareEpoch;
    m_pendingFirmwareValues.clear();
    m_pendingFirmwareReplies.clear();
    foreach (const QString &propertyName, firmwareProperties()) {
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            m_iface->asyncCall(propertyName), this);
        watcher->setProperty("propertyName", propertyName);
        watcher->setProperty("firmwareEpoch", QVariant::fromValue<qulonglong>(epoch));
        watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
        connect(watcher, &QDBusPendingCallWatcher::finished,
                this, &Pebble::firmwarePropertyReplyFinished);
    }
}

void Pebble::firmwarePropertyReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString propertyName = watcher->property("propertyName").toString();
    const quint64 epoch = watcher->property("firmwareEpoch").toULongLong();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || epoch != m_firmwareEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() != 1) {
        qWarning() << "Could not refresh" << propertyName << reply.errorMessage();
        return;
    }

    m_pendingFirmwareValues.insert(propertyName, reply.arguments().first());
    m_pendingFirmwareReplies.insert(propertyName);
    foreach (const QString &requiredProperty, firmwareProperties()) {
        if (!m_pendingFirmwareReplies.contains(requiredProperty)) {
            return;
        }
    }

    const bool available = m_pendingFirmwareValues.value(
        QStringLiteral("FirmwareUpgradeAvailable")).toBool();
    const QString releaseNotes = available
        ? m_pendingFirmwareValues.value(QStringLiteral("FirmwareReleaseNotes")).toString()
        : QString();
    const QString candidateVersion = available
        ? m_pendingFirmwareValues.value(QStringLiteral("CandidateFirmwareVersion")).toString()
        : QString();
    const bool upgrading = m_pendingFirmwareValues.value(
        QStringLiteral("UpgradingFirmware")).toBool();

    if (m_firmwareUpgradeAvailable != available ||
            m_firmwareReleaseNotes != releaseNotes ||
            m_candidateVersion != candidateVersion) {
        m_firmwareUpgradeAvailable = available;
        m_firmwareReleaseNotes = releaseNotes;
        m_candidateVersion = candidateVersion;
        emit firmwareUpgradeAvailableChanged();
    }
    if (m_upgradingFirmware != upgrading) {
        m_upgradingFirmware = upgrading;
        emit upgradingFirmwareChanged();
    }
}

void Pebble::requestScreenshot()
{
    sendVoidCommand(QStringLiteral("RequestScreenshot"));
}

void Pebble::removeScreenshot(const QString &filename)
{
    qDebug() << "removing screenshot" << filename;
    sendVoidCommand(QStringLiteral("RemoveScreenshot"), QVariantList() << filename);
}

void Pebble::performFirmwareUpgrade()
{
    sendVoidCommand(QStringLiteral("PerformFirmwareUpgrade"));
}

void Pebble::dumpLogs(const QString &filename)
{
    if (m_logDumpPending) {
        return;
    }
    m_logDumpPending = true;
    const quint64 logDumpEpoch = ++m_logDumpEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCallWithArgumentList(QStringLiteral("DumpLogs"),
                                           QVariantList() << filename), this);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("logDumpEpoch",
                         QVariant::fromValue<qulonglong>(logDumpEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebble::dumpLogsReplyFinished);
}

void Pebble::dumpLogsReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 logDumpEpoch = watcher->property("logDumpEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || logDumpEpoch != m_logDumpEpoch ||
            !m_logDumpPending) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "DumpLogs failed:" << reply.errorMessage();
        m_logDumpPending = false;
        ++m_logDumpEpoch;
        emit logsDumped(false);
    }
}

void Pebble::logsDumpedFromService(bool success)
{
    if (!m_logDumpPending) {
        return;
    }
    m_logDumpPending = false;
    ++m_logDumpEpoch;
    emit logsDumped(success);
}
