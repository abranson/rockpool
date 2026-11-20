/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CORESTOREACCOUNT_H
#define CORESTOREACCOUNT_H

#include <QByteArray>
#include <QDBusAbstractInterface>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

#include <functional>

class QDBusServiceWatcher;
class QJsonObject;
class QNetworkReply;
class QNetworkRequest;
class QTcpSocket;

class CoreStoreAccountManager : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit CoreStoreAccountManager(const QString &owner, QObject *parent);

signals:
    void CoreAccountChanged();
};

class CoreStoreAccount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY accountChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY accountChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY accountChanged)
    Q_PROPERTY(QString email READ email NOTIFY accountChanged)
    Q_PROPERTY(QString error READ error NOTIFY accountChanged)

public:
    explicit CoreStoreAccount(QObject *parent = nullptr);
    ~CoreStoreAccount() override;

    bool signedIn() const { return !m_profile.isEmpty(); }
    bool busy() const { return m_busy || m_writingAccount; }
    QString displayName() const { return m_profile.value(1); }
    QString email() const { return m_profile.value(2); }
    QString error() const { return m_error; }

    Q_INVOKABLE void login();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void browserOpenFailed();

signals:
    void accountChanged();
    void openBrowser(const QUrl &url);

private:
    // Status is zero for a TLS, transport or response framing failure.
    using ReplyHandler = std::function<void(int, const QJsonObject &)>;
    void request(QNetworkRequest request, const QByteArray &body, ReplyHandler handler);
    void postBroker(const QString &path, const QJsonObject &body, ReplyHandler handler);
    void postFirebase(const QString &path, const QJsonObject &body, ReplyHandler handler);
    void abortRequest();

    void acceptCallbacks();
    void readCallback(QTcpSocket *socket);
    void closeCallbacks();
    void exchange();
    void signInWithCustomToken(const QString &token);
    void verifyStore(const QString &idToken, const QString &uid, std::function<void(int, bool)> handler);
    void finishLogin(const QString &error = QString());

    void loadDaemonOwner();
    void setDaemonOwner(const QString &owner);
    void refreshAccount();
    void writeAccount(const QString &method, const QVariantList &arguments,
                      std::function<void(bool)> complete);

    QNetworkAccessManager m_network;
    QTcpServer m_listener;
    QTimer m_loginTimer;
    QTimer m_pollTimer;
    QTimer m_requestTimer;
    QDBusServiceWatcher *m_serviceWatcher;
    CoreStoreAccountManager *m_manager = nullptr;
    QHash<QTcpSocket *, QByteArray> m_pendingCallbacks;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_response;
    QByteArray m_callbackPath;
    QString m_publicFlowId;
    QString m_exchangeHandle;
    QString m_daemonOwner;
    QStringList m_profile;
    QString m_error;
    quint64 m_ownerEpoch = 0;
    quint64 m_readEpoch = 0;
    bool m_busy = false;
    bool m_writingAccount = false;
};

#endif
