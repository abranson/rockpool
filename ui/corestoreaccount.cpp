/* SPDX-License-Identifier: Apache-2.0 */
#include "corestoreaccount.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUuid>

namespace {
const auto BrokerHost = QStringLiteral("appstore-api.repebble.com");
const auto AuthorizePath = QStringLiteral("/api/v1/cli-oauth/authorize");
// The public Firebase web key and broker client ID are used by the official Pebble CLI.
const auto FirebaseKey = QStringLiteral("AIzaSyBZ9Cdvwwv9At2lPmc8TxyyEqSXGXejGvc");
const QByteArray BrokerClient("pebble-cli-public");
const int LoginTimeout = 5 * 60 * 1000;
const int RequestTimeout = 15 * 1000;
const int MaxCallback = 8192;
const int MaxResponse = 512 * 1024;
const auto DaemonService = QStringLiteral("org.rockpool");
const auto ManagerPath = QStringLiteral("/org/rockpool/Manager");
const char ManagerInterface[] = "org.rockpool.Manager";

QUrl endpoint(const QString &host, const QString &path)
{
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(host);
    url.setPath(path);
    return url;
}

QUrl firebaseEndpoint(const QString &host, const QString &path)
{
    QUrl url = endpoint(host, path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), FirebaseKey);
    url.setQuery(query);
    return url;
}

bool isCredential(const QString &value, int limit = 16 * 1024)
{
    if (value.isEmpty() || value.size() > limit)
        return false;

    for (const QChar character : value) {
        const auto code = character.unicode();
        if (code < 0x21 || code > 0x7e)
            return false;
    }
    return true;
}

QDBusPendingCall managerCall(const QString &owner, const QString &method,
                             const QVariantList &arguments = QVariantList())
{
    auto message = QDBusMessage::createMethodCall(owner, ManagerPath, QString::fromLatin1(ManagerInterface), method);
    message.setAutoStartService(false);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().asyncCall(message, RequestTimeout);
}

bool isFlowValue(const QString &value, int limit)
{
    static const QRegularExpression allowed(QStringLiteral("\\A[A-Za-z0-9_.~-]+\\z"));
    return value.size() <= limit && allowed.match(value).hasMatch();
}

int boundedSeconds(const QJsonValue &value, int maximum)
{
    return value.isString() ? qBound(0, value.toString().toInt(), maximum)
                            : qBound(0, value.toInt(), maximum);
}

QByteArray json(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}

bool success(int status)
{
    return status >= 200 && status < 300;
}

void callbackResponse(QTcpSocket *socket, const QByteArray &status, const QByteArray &message)
{
    socket->write("HTTP/1.1 " + status + "\r\n"
                  "Connection: close\r\n"
                  "Cache-Control: no-store\r\n"
                  "Referrer-Policy: no-referrer\r\n"
                  "X-Content-Type-Options: nosniff\r\n"
                  "Content-Type: text/plain; charset=utf-8\r\n"
                  "Content-Length: " + QByteArray::number(message.size()) + "\r\n\r\n" + message);
    socket->disconnectFromHost();
}
}

CoreStoreAccountManager::CoreStoreAccountManager(const QString &owner, QObject *parent)
    : QDBusAbstractInterface(owner, ManagerPath, ManagerInterface, QDBusConnection::sessionBus(), parent)
{
}

CoreStoreAccount::CoreStoreAccount(QObject *parent)
    : QObject(parent),
      m_serviceWatcher(new QDBusServiceWatcher(DaemonService, QDBusConnection::sessionBus(),
                                               QDBusServiceWatcher::WatchForOwnerChange, this))
{
    m_listener.setMaxPendingConnections(4);
    connect(&m_listener, &QTcpServer::newConnection, this, &CoreStoreAccount::acceptCallbacks);

    m_loginTimer.setSingleShot(true);
    connect(&m_loginTimer, &QTimer::timeout, this, [this] {
        finishLogin(tr("Core Devices sign-in timed out."));
    });
    m_requestTimer.setSingleShot(true);
    connect(&m_requestTimer, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &CoreStoreAccount::exchange);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) { setDaemonOwner(owner); });
    loadDaemonOwner();
}

CoreStoreAccount::~CoreStoreAccount()
{
    abortRequest();
    closeCallbacks();
}

void CoreStoreAccount::login()
{
    if (busy())
        return;
    if (m_daemonOwner.isEmpty()) {
        m_error = tr("The Rockpool daemon is unavailable.");
        emit accountChanged();
        return;
    }

    abortRequest();
    if (!m_listener.listen(QHostAddress::LocalHost)) {
        m_error = tr("Could not start Core Devices sign-in.");
        emit accountChanged();
        return;
    }

    m_busy = true;
    m_error.clear();
    auto nonce = QUuid::createUuid().toString();
    nonce.remove(QLatin1Char('{')).remove(QLatin1Char('}')).remove(QLatin1Char('-'));
    m_callbackPath = "/rockpool-" + nonce.toLatin1();
    QUrl callback;
    callback.setScheme(QStringLiteral("http"));
    callback.setHost(QStringLiteral("127.0.0.1"));
    callback.setPort(m_listener.serverPort());
    callback.setPath(QString::fromLatin1(m_callbackPath));
    m_loginTimer.start(LoginTimeout);
    emit accountChanged();
    if (!m_busy)
        return;

    postBroker(QStringLiteral("/start"), {{QStringLiteral("callback_url"), callback.toString()}},
               [this](int status, const QJsonObject &result) {
        const QString handle = result.value(QStringLiteral("flow_id")).toString();
        const QUrl url(result.value(QStringLiteral("auth_url")).toString(), QUrl::StrictMode);
        const QUrlQuery query(url);
        const auto ids = query.allQueryItemValues(QStringLiteral("flow_id"), QUrl::FullyDecoded);
        const auto states = query.allQueryItemValues(QStringLiteral("state"), QUrl::FullyDecoded);
        const int expires = boundedSeconds(result.value(QStringLiteral("expires_in")), LoginTimeout / 1000);
        // The browser gets the public flow ID and browser state. The broker returns a
        // separate opaque redemption handle prefixed with that public identifier.
        if (!success(status) || !url.isValid() || url.scheme() != QStringLiteral("https")
                || (url.authority() != BrokerHost && url.authority() != BrokerHost + QStringLiteral(":443"))
                || url.path(QUrl::FullyEncoded) != AuthorizePath
                || url.hasFragment() || ids.size() != 1 || states.size() != 1
                || !isFlowValue(ids.first(), 128) || !isFlowValue(states.first(), 256)
                || !isFlowValue(handle, 512) || !handle.startsWith(ids.first() + QLatin1Char('.'))
                || handle.size() <= ids.first().size() + 1) {
            finishLogin(tr("Could not start Core Devices sign-in."));
            return;
        }

        m_publicFlowId = ids.first();
        m_exchangeHandle = handle;
        if (expires)
            m_loginTimer.start(qMin(m_loginTimer.remainingTime(), expires * 1000));

        QUrl browser = endpoint(BrokerHost, AuthorizePath);
        QUrlQuery browserQuery;
        browserQuery.addQueryItem(QStringLiteral("flow_id"), ids.first());
        browserQuery.addQueryItem(QStringLiteral("state"), states.first());
        browser.setQuery(browserQuery);
        emit openBrowser(browser);
    });
}

void CoreStoreAccount::cancelLogin()
{
    if (m_busy && !m_writingAccount)
        finishLogin();
}

void CoreStoreAccount::browserOpenFailed()
{
    if (m_busy && !m_writingAccount)
        finishLogin(tr("Could not open the browser for Core Devices sign-in."));
}

void CoreStoreAccount::logout()
{
    if (m_writingAccount)
        return;
    if (m_busy)
        finishLogin();
    if (m_daemonOwner.isEmpty()) {
        m_error = tr("The Rockpool daemon is unavailable.");
        emit accountChanged();
        return;
    }

    writeAccount(QStringLiteral("ClearCoreAccount"), {}, [this](bool saved) {
        if (saved)
            m_profile.clear();
        m_error = saved ? QString() : tr("Could not remove the saved Core Devices account.");
        emit accountChanged();
    });
}

void CoreStoreAccount::finishLogin(const QString &error)
{
    abortRequest();
    closeCallbacks();
    m_loginTimer.stop();
    m_pollTimer.stop();
    m_callbackPath.clear();
    m_publicFlowId.clear();
    m_exchangeHandle.clear();
    m_busy = false;
    m_error = error;
    emit accountChanged();
}

void CoreStoreAccount::acceptCallbacks()
{
    while (m_listener.hasPendingConnections()) {
        auto socket = m_listener.nextPendingConnection();
        if (!socket)
            continue;
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        if (m_pendingCallbacks.size() >= 4) {
            socket->abort();
            continue;
        }
        socket->setReadBufferSize(MaxCallback + 1);
        m_pendingCallbacks.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_pendingCallbacks.remove(socket);
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readCallback(socket); });
        QTimer::singleShot(5000, socket, [socket] { socket->abort(); });
        readCallback(socket);
    }
}

void CoreStoreAccount::readCallback(QTcpSocket *socket)
{
    auto pending = m_pendingCallbacks.find(socket);
    if (pending == m_pendingCallbacks.end())
        return;

    pending.value() += socket->read(MaxCallback + 1 - pending.value().size());
    if (pending.value().size() > MaxCallback) {
        callbackResponse(socket, "431 Request Header Fields Too Large", QByteArray());
        return;
    }
    if (!pending.value().contains("\r\n\r\n"))
        return;

    const auto parts = pending.value().left(pending.value().indexOf("\r\n")).split(' ');
    m_pendingCallbacks.erase(pending);
    disconnect(socket, nullptr, this, nullptr);
    const QByteArray requestTarget = parts.value(1);
    const QUrl target = QUrl::fromEncoded(requestTarget, QUrl::StrictMode);
    const QUrlQuery query(target);
    const auto flowIds = query.allQueryItemValues(QStringLiteral("flow_id"), QUrl::FullyDecoded);
    const auto statuses = query.allQueryItemValues(QStringLiteral("broker_status"), QUrl::FullyDecoded);
    if (parts.size() != 3 || parts.first() != "GET"
            || (parts.last() != "HTTP/1.1" && parts.last() != "HTTP/1.0")
            || requestTarget.split('?').first() != m_callbackPath || !target.isValid() || target.hasFragment()
            || m_exchangeHandle.isEmpty() || flowIds.size() > 1 || statuses.size() > 1
            || (!flowIds.isEmpty() && flowIds.first() != m_publicFlowId && flowIds.first() != m_exchangeHandle)) {
        callbackResponse(socket, "404 Not Found", QByteArray());
        return;
    }

    const QString status = statuses.value(0);
    if (status == QStringLiteral("error") || query.hasQueryItem(QStringLiteral("error"))) {
        callbackResponse(socket, "200 OK", tr("Sign-in was not completed. Return to Rockpool.").toUtf8());
        finishLogin(tr("Core Devices sign-in was declined or failed."));
    } else if (statuses.isEmpty() || status == QStringLiteral("ok") || status == QStringLiteral("success")) {
        callbackResponse(socket, "200 OK", tr("Return to Rockpool to finish signing in.").toUtf8());
        closeCallbacks();
        exchange();
    } else {
        callbackResponse(socket, "400 Bad Request", QByteArray());
    }
}

void CoreStoreAccount::closeCallbacks()
{
    m_listener.close();
    const auto sockets = m_pendingCallbacks.keys();
    m_pendingCallbacks.clear();
    for (auto socket : sockets)
        socket->abort();
}

void CoreStoreAccount::exchange()
{
    if (!m_busy || m_exchangeHandle.isEmpty())
        return;
    postBroker(QStringLiteral("/exchange"), {{QStringLiteral("flow_id"), m_exchangeHandle}},
               [this](int status, const QJsonObject &result) {
        const auto outcome = result.value(QStringLiteral("status")).toString();
        if (success(status) && outcome == QStringLiteral("pending")) {
            m_pollTimer.start(500);
            return;
        }
        const auto token = result.value(QStringLiteral("credential")).toString();
        if (!success(status) || outcome != QStringLiteral("ok")
                || result.value(QStringLiteral("credential_type")).toString() != QStringLiteral("custom_token")
                || !isCredential(token)) {
            finishLogin(tr("Could not complete Core Devices sign-in."));
            return;
        }
        m_exchangeHandle.clear();
        signInWithCustomToken(token);
    });
}

void CoreStoreAccount::signInWithCustomToken(const QString &token)
{
    postFirebase(QStringLiteral("/v1/accounts:signInWithCustomToken"),
                 {{QStringLiteral("token"), token}, {QStringLiteral("returnSecureToken"), true}},
                 [this](int status, const QJsonObject &result) {
        const auto idToken = result.value(QStringLiteral("idToken")).toString();
        const auto refreshToken = result.value(QStringLiteral("refreshToken")).toString();
        const int expires = boundedSeconds(result.value(QStringLiteral("expiresIn")), 24 * 60 * 60);
        if (!success(status) || !isCredential(idToken) || !isCredential(refreshToken) || !expires) {
            finishLogin(tr("Could not complete Core Devices sign-in."));
            return;
        }

        postFirebase(QStringLiteral("/v1/accounts:lookup"), {{QStringLiteral("idToken"), idToken}},
                     [this, refreshToken, idToken](int status, const QJsonObject &profile) {
            const auto users = profile.value(QStringLiteral("users")).toArray();
            const auto user = users.size() == 1 ? users.first().toObject() : QJsonObject();
            const QStringList candidate{
                user.value(QStringLiteral("localId")).toString(),
                user.value(QStringLiteral("displayName")).toString().left(256),
                user.value(QStringLiteral("email")).toString().left(320),
            };
            if (!success(status) || !isCredential(candidate.at(0), 128)) {
                finishLogin(tr("Could not verify the Core Devices account profile."));
                return;
            }

            verifyStore(idToken, candidate.at(0), [this, candidate, refreshToken](int, bool verified) {
                if (!verified) {
                    finishLogin(tr("Could not verify the Core Devices appstore account."));
                    return;
                }
                writeAccount(QStringLiteral("SetCoreAccount"),
                             {refreshToken, candidate.at(0), candidate.at(1), candidate.at(2)},
                             [this, candidate](bool saved) {
                    if (saved)
                        m_profile = candidate;
                    finishLogin(saved ? QString() : tr("Could not save the Core Devices account."));
                });
            });
        });
    });
}

void CoreStoreAccount::verifyStore(const QString &idToken, const QString &uid,
                                   std::function<void(int, bool)> handler)
{
    QNetworkRequest call(endpoint(BrokerHost, QStringLiteral("/api/v1/users/me")));
    call.setRawHeader("Authorization", "Bearer " + idToken.toLatin1());
    request(call, QByteArray(), [uid, handler](int status, const QJsonObject &profile) {
        handler(status, success(status) && profile.value(QStringLiteral("uid")).toString() == uid);
    });
}

void CoreStoreAccount::loadDaemonOwner()
{
    const auto epoch = m_ownerEpoch;
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("GetNameOwner"));
    call << DaemonService;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, RequestTimeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, epoch] {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (epoch == m_ownerEpoch && reply.isValid())
            setDaemonOwner(reply.value());
    });
}

void CoreStoreAccount::setDaemonOwner(const QString &owner)
{
    if (owner == m_daemonOwner)
        return;
    ++m_ownerEpoch;
    m_daemonOwner = owner;
    m_writingAccount = false;
    m_profile.clear();

    if (m_manager) {
        disconnect(m_manager, nullptr, this, nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
    }
    if (m_busy)
        finishLogin(tr("The Rockpool daemon disconnected during Core Devices sign-in."));
    else
        emit accountChanged();

    if (owner.isEmpty())
        return;
    // A unique service name does not require a blocking owner lookup in Qt.
    m_manager = new CoreStoreAccountManager(owner, this);
    const auto epoch = m_ownerEpoch;
    connect(m_manager, &CoreStoreAccountManager::CoreAccountChanged, this, [this, epoch] {
        if (epoch == m_ownerEpoch)
            refreshAccount();
    });
    refreshAccount();
}

void CoreStoreAccount::refreshAccount()
{
    if (m_daemonOwner.isEmpty() || m_writingAccount)
        return;

    const auto ownerEpoch = m_ownerEpoch;
    const auto readEpoch = ++m_readEpoch;
    auto *watcher = new QDBusPendingCallWatcher(managerCall(m_daemonOwner, QStringLiteral("GetCoreAccount")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, ownerEpoch, readEpoch] {
        const QDBusPendingReply<QStringList> reply = *watcher;
        watcher->deleteLater();
        if (ownerEpoch != m_ownerEpoch || readEpoch != m_readEpoch || !reply.isValid())
            return;
        const auto profile = reply.value();
        if (!profile.isEmpty() && (profile.size() != 3 || !isCredential(profile.at(0), 128)))
            return;

        if (profile != m_profile) {
            m_profile = profile;
            emit accountChanged();
        }
    });
}

void CoreStoreAccount::writeAccount(const QString &method, const QVariantList &arguments,
                                    std::function<void(bool)> complete)
{
    if (m_daemonOwner.isEmpty()) {
        complete(false);
        return;
    }
    const auto ownerEpoch = m_ownerEpoch;
    ++m_readEpoch;
    m_writingAccount = true;
    auto *watcher = new QDBusPendingCallWatcher(managerCall(m_daemonOwner, method, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, ownerEpoch, complete] {
        const QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (ownerEpoch != m_ownerEpoch)
            return;
        m_writingAccount = false;
        complete(reply.isValid() && reply.value());
        refreshAccount();
    });
    emit accountChanged();
}

void CoreStoreAccount::postBroker(const QString &path, const QJsonObject &body, ReplyHandler handler)
{
    QNetworkRequest call(endpoint(BrokerHost, QStringLiteral("/api/v1/cli-oauth") + path));
    call.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    call.setRawHeader("X-Pebble-Client-Key", BrokerClient);
    request(call, json(body), handler);
}

void CoreStoreAccount::postFirebase(const QString &path, const QJsonObject &body, ReplyHandler handler)
{
    QNetworkRequest call(firebaseEndpoint(QStringLiteral("identitytoolkit.googleapis.com"), path));
    call.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request(call, json(body), handler);
}

void CoreStoreAccount::request(QNetworkRequest call, const QByteArray &body, ReplyHandler handler)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    call.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
#else
    call.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
#endif
    call.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    call.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    call.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    call.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    call.setRawHeader("Accept", "application/json");
    auto reply = body.isNull() ? m_network.get(call) : m_network.post(call, body);
    m_reply = reply;
    m_response.clear();
    reply->setReadBufferSize(MaxResponse + 1);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_response += reply->read(MaxResponse + 1 - m_response.size());
        if (m_response.size() > MaxResponse)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler] {
        m_requestTimer.stop();
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_response += reply->read(MaxResponse + 1 - m_response.size());
        const auto result = m_response.size() <= MaxResponse ? QJsonDocument::fromJson(m_response) : QJsonDocument();
        m_response.clear();
        const bool validResponse = status >= 400 ||
                (result.isObject() && success(status) && reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        handler(validResponse ? status : 0, result.object());
    });
    m_requestTimer.start(RequestTimeout);
}

void CoreStoreAccount::abortRequest()
{
    m_requestTimer.stop();
    if (!m_reply)
        return;
    auto reply = m_reply;
    m_reply = nullptr;
    m_response.clear();
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}
