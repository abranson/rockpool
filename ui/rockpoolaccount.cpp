#include "rockpoolaccount.h"
#include "rockpooloperation.h"

#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QUrl>

namespace {
const char ACCOUNT_INTERFACE[] = "io.rebble.libpebble3.Account1";
const char ACCOUNT_PATH[] = "/io/rebble/libpebble3/Manager";
const char OPERATION_PATH_PREFIX[] = "/io/rebble/libpebble3/Operations/";
const char ROCKPOOL_SERVICE[] = "io.rebble.libpebble3";

bool validPercentEncoding(const QString &value)
{
    for (int i = 0; i < value.size(); ++i) {
        if (value.at(i) != QLatin1Char('%')) {
            continue;
        }
        if (i + 2 >= value.size()) {
            return false;
        }
        const QChar high = value.at(i + 1).toLower();
        const QChar low = value.at(i + 2).toLower();
        const bool highValid = high.isDigit()
            || (high >= QLatin1Char('a') && high <= QLatin1Char('f'));
        const bool lowValid = low.isDigit()
            || (low >= QLatin1Char('a') && low <= QLatin1Char('f'));
        if (!highValid || !lowValid) {
            return false;
        }
        i += 2;
    }
    return true;
}

QString queryToken(const QString &url)
{
    const QString questionMarker = QStringLiteral("?access_token=");
    const QString ampersandMarker = QStringLiteral("&access_token=");
    int marker = url.indexOf(questionMarker);
    int markerLength = questionMarker.size();
    const int ampersand = url.indexOf(ampersandMarker);
    if (marker < 0 || (ampersand >= 0 && ampersand < marker)) {
        marker = ampersand;
        markerLength = ampersandMarker.size();
    }
    if (marker < 0) {
        return QString();
    }

    const int start = marker + markerLength;
    int end = url.indexOf(QLatin1Char('&'), start);
    const int fragment = url.indexOf(QLatin1Char('#'), start);
    if (end < 0 || (fragment >= 0 && fragment < end)) {
        end = fragment;
    }
    if (end < 0) {
        end = url.size();
    }
    const QString encoded = url.mid(start, end - start);
    if (encoded.isEmpty() || !validPercentEncoding(encoded)) {
        return QString();
    }
    return QUrl::fromPercentEncoding(encoded.toUtf8());
}
}

RockpoolPropertiesInterface::RockpoolPropertiesInterface(QObject *parent):
    QDBusAbstractInterface(
        QString::fromLatin1(ROCKPOOL_SERVICE),
        QString::fromLatin1(ACCOUNT_PATH),
        "org.freedesktop.DBus.Properties",
        QDBusConnection::sessionBus(),
        parent)
{
}

RockpoolAccountInterface::RockpoolAccountInterface(QObject *parent):
    QDBusAbstractInterface(
        QString::fromLatin1(ROCKPOOL_SERVICE),
        QString::fromLatin1(ACCOUNT_PATH),
        ACCOUNT_INTERFACE,
        QDBusConnection::sessionBus(),
        parent)
{
}

RockpoolAccount::RockpoolAccount(QObject *parent):
    QObject(parent)
{
    resetInterfaces();
    m_serviceWatcher = new QDBusServiceWatcher(
        QString::fromLatin1(ROCKPOOL_SERVICE), QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &RockpoolAccount::serviceOwnerChanged);
    refreshProperties();
}

bool RockpoolAccount::authenticated() const
{
    return m_authenticated;
}

QString RockpoolAccount::name() const
{
    return m_name;
}

QString RockpoolAccount::email() const
{
    return m_email;
}

bool RockpoolAccount::tokenPending() const
{
    return m_tokenPending;
}

QString RockpoolAccount::tokenError() const
{
    return m_tokenError;
}

QString RockpoolAccount::oauthTokenFromCallback(const QString &callbackUrl)
{
    const QString prefix =
        QStringLiteral("pebble://custom-boot-config-url/");
    if (!callbackUrl.startsWith(prefix)) {
        return QString();
    }

    QString payload = callbackUrl.mid(prefix.size());
    for (int layer = 0; layer < 3; ++layer) {
        const QString token = queryToken(payload);
        if (!token.isEmpty()) {
            return token;
        }
        if (!validPercentEncoding(payload)) {
            return QString();
        }
        const QString decoded = QUrl::fromPercentEncoding(payload.toUtf8());
        if (decoded == payload) {
            return QString();
        }
        payload = decoded;
    }
    return QString();
}

void RockpoolAccount::setOAuthToken(const QString &token)
{
    const quint64 tokenEpoch = ++m_tokenEpoch;
    setTokenError(QString());
    setTokenPending(true);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_interface->asyncCall(QStringLiteral("SetOAuthToken"), token), this);
    watcher->setProperty("tokenEpoch",
                         QVariant::fromValue<qulonglong>(tokenEpoch));
    watcher->setProperty("accountOwnerEpoch",
                         QVariant::fromValue<qulonglong>(m_ownerEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &RockpoolAccount::tokenReplyFinished);
}

void RockpoolAccount::resetInterfaces()
{
    if (m_interface) {
        m_interface->deleteLater();
    }
    if (m_properties) {
        m_properties->deleteLater();
    }
    m_interface = new RockpoolAccountInterface(this);
    m_properties = new RockpoolPropertiesInterface(this);
    const quint64 ownerEpoch = m_ownerEpoch;
    connect(m_properties, &RockpoolPropertiesInterface::PropertiesChanged,
            this,
            [this, ownerEpoch](const QString &interfaceName,
                               const QVariantMap &changed,
                               const QStringList &invalidated) {
        if (ownerEpoch == m_ownerEpoch) {
            propertiesChanged(interfaceName, changed, invalidated);
        }
    });
}

void RockpoolAccount::refreshProperties()
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_properties->asyncCall(QStringLiteral("GetAll"),
                                QString::fromLatin1(ACCOUNT_INTERFACE)), this);
    watcher->setProperty("accountPropertiesEpoch",
                         QVariant::fromValue<qulonglong>(m_propertiesEpoch));
    watcher->setProperty("accountOwnerEpoch",
                         QVariant::fromValue<qulonglong>(m_ownerEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &RockpoolAccount::propertiesReplyFinished);
}

void RockpoolAccount::propertiesReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    const quint64 epoch =
        watcher->property("accountPropertiesEpoch").toULongLong();
    const quint64 ownerEpoch =
        watcher->property("accountOwnerEpoch").toULongLong();
    watcher->deleteLater();
    if (ownerEpoch != m_ownerEpoch || epoch != m_propertiesEpoch) {
        return;
    }
    if (reply.isError()) {
        qWarning() << "Could not refresh Rockpool account properties:"
                   << reply.error().message();
        return;
    }
    applyProperties(reply.value());
}

void RockpoolAccount::tokenReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    const quint64 tokenEpoch = watcher->property("tokenEpoch").toULongLong();
    const quint64 ownerEpoch =
        watcher->property("accountOwnerEpoch").toULongLong();
    watcher->deleteLater();
    if (ownerEpoch != m_ownerEpoch || tokenEpoch != m_tokenEpoch) {
        return;
    }
    if (reply.isError()) {
        qWarning() << "SetOAuthToken failed:" << reply.error().message();
        const QString error = reply.error().message().isEmpty()
            ? reply.error().name() : reply.error().message();
        finishTokenAttempt(tokenEpoch, ownerEpoch, error);
        return;
    }

    const QString path = reply.value().path();
    if (!path.startsWith(QString::fromLatin1(OPERATION_PATH_PREFIX)) ||
            path.size() <= int(sizeof(OPERATION_PATH_PREFIX) - 1)) {
        finishTokenAttempt(tokenEpoch, ownerEpoch,
                           tr("Rockpool returned an invalid account operation"));
        return;
    }

    RockpoolOperation *operation = new RockpoolOperation(reply.value(), this);
    m_tokenOperations.insert(operation);
    connect(operation, &RockpoolOperation::completed, this,
            [this, operation, tokenEpoch, ownerEpoch](bool success) {
        tokenOperationCompleted(operation, tokenEpoch, ownerEpoch, success);
    });
}

void RockpoolAccount::tokenOperationCompleted(RockpoolOperation *operation,
                                              quint64 tokenEpoch,
                                              quint64 ownerEpoch,
                                              bool success)
{
    m_tokenOperations.remove(operation);
    QString error;
    if (!success) {
        error = operation->errorDetail();
        if (error.isEmpty()) {
            error = operation->error();
        }
        if (error.isEmpty()) {
            error = tr("Rockpool rejected the account change");
        }
    }
    finishTokenAttempt(tokenEpoch, ownerEpoch, error);
    operation->deleteLater();
}

void RockpoolAccount::finishTokenAttempt(quint64 tokenEpoch,
                                         quint64 ownerEpoch,
                                         const QString &error)
{
    if (tokenEpoch != m_tokenEpoch || ownerEpoch != m_ownerEpoch) {
        return;
    }
    setTokenError(error);
    setTokenPending(false);
    ++m_propertiesEpoch;
    refreshProperties();
}

void RockpoolAccount::setTokenPending(bool pending)
{
    if (m_tokenPending == pending) {
        return;
    }
    m_tokenPending = pending;
    emit tokenPendingChanged();
}

void RockpoolAccount::setTokenError(const QString &error)
{
    if (m_tokenError == error) {
        return;
    }
    m_tokenError = error;
    emit tokenErrorChanged();
}

void RockpoolAccount::serviceOwnerChanged(const QString &service,
                                          const QString &oldOwner,
                                          const QString &newOwner)
{
    Q_UNUSED(service)
    Q_UNUSED(oldOwner)

    ++m_ownerEpoch;
    ++m_propertiesEpoch;
    ++m_tokenEpoch;
    resetInterfaces();

    if (m_tokenPending) {
        setTokenError(tr("Rockpool account service changed"));
        setTokenPending(false);
    }

    QVariantMap unavailable;
    unavailable.insert(QStringLiteral("Authenticated"), false);
    unavailable.insert(QStringLiteral("Name"), QString());
    unavailable.insert(QStringLiteral("Email"), QString());
    applyProperties(unavailable);
    if (!newOwner.isEmpty()) {
        refreshProperties();
    }
}

void RockpoolAccount::propertiesChanged(const QString &interfaceName,
                                        const QVariantMap &changed,
                                        const QStringList &invalidated)
{
    if (interfaceName != QString::fromLatin1(ACCOUNT_INTERFACE)) {
        return;
    }
    ++m_propertiesEpoch;
    applyProperties(changed);
    if (invalidated.contains(QStringLiteral("Authenticated")) ||
            invalidated.contains(QStringLiteral("Name")) ||
            invalidated.contains(QStringLiteral("Email"))) {
        refreshProperties();
    }
}

void RockpoolAccount::applyProperties(const QVariantMap &properties)
{
    QVariantMap::ConstIterator it =
        properties.constFind(QStringLiteral("Authenticated"));
    if (it != properties.constEnd()) {
        const bool authenticated = it.value().toBool();
        if (m_authenticated != authenticated) {
            m_authenticated = authenticated;
            emit authenticatedChanged();
        }
    }

    it = properties.constFind(QStringLiteral("Name"));
    if (it != properties.constEnd()) {
        const QString name = it.value().toString();
        if (m_name != name) {
            m_name = name;
            emit nameChanged();
        }
    }

    it = properties.constFind(QStringLiteral("Email"));
    if (it != properties.constEnd()) {
        const QString email = it.value().toString();
        if (m_email != email) {
            m_email = email;
            emit emailChanged();
        }
    }
}
