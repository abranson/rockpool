#ifndef APPMSGMANAGER_H
#define APPMSGMANAGER_H

#include <functional>
#include <QUuid>
#include <QQueue>

#include "watchconnection.h"
#include "appmanager.h"

class AppMsgManager : public QObject
{
    Q_OBJECT

public:
    enum AppMessage {
        AppMessagePush = 1,
        AppMessageRequest = 2,
        AppMessageAck = 0xFF,
        AppMessageNack = 0x7F
    };
    enum LauncherMessage {
        LauncherActionStart = 1,
        LauncherActionStop = 2
    };

    explicit AppMsgManager(Pebble *pebble, AppManager *apps, WatchConnection *connection);

    void send(const QUuid &uuid, const QVariantMap &data,
              const std::function<void()> &ackCallback,
              const std::function<void()> &nackCallback);

    typedef std::function<bool(const QVariantMap &)> MessageHandlerFunc;
    void setMessageHandler(const QUuid &uuid, MessageHandlerFunc func);
    void clearMessageHandler(const QUuid &uuid);

    uint lastTransactionId() const;
    uint nextTransactionId() const;

public slots:
    void send(const QUuid &uuid, const QVariantMap &data);
    void launchApp(const QUuid &uuid);
    void closeApp(const QUuid &uuid);

signals:
    void appStarted(const QUuid &uuid);
    void appStopped(const QUuid &uuid);

private:
    WatchConnection::Dict mapAppKeys(const QUuid &uuid, const QVariantMap &data);
    QVariantMap mapAppKeys(const QUuid &uuid, const WatchConnection::Dict &dict);

    static bool unpackAppLaunchMessage(const QByteArray &msg, QUuid *uuid);
    static bool unpackPushMessage(const QByteArray &msg, quint8 *transaction, QUuid *uuid, WatchConnection::Dict *dict);

    static QByteArray buildPushMessage(quint8 transaction, const QUuid &uuid, const WatchConnection::Dict &dict);
    static QByteArray buildLaunchMessage(quint8 messageType, const QUuid &uuid);
    static QByteArray buildAckMessage(quint8 transaction);
    static QByteArray buildNackMessage(quint8 transaction);

    void handleLauncherPushMessage(const QByteArray &data);
    void handlePushMessage(const QByteArray &data);
    void handleAckMessage(const QByteArray &data, bool ack);

    void transmitNextPendingTransaction();
    void abortPendingTransactions();

private slots:
    void handleWatchConnectedChanged();
    void handlePebbleConnected();
    void handleTimeout();

    void handleAppLaunchMessage(const QByteArray &data);
    void handleLauncherMessage(const QByteArray &data);
    void handleApplicationMessage(const QByteArray &data);

private:
    Pebble *m_pebble;
    AppManager *apps;
    WatchConnection *m_connection;
    QHash<QUuid, MessageHandlerFunc> _handlers;
    quint8 _lastTransactionId;
    QUuid m_currentUuid;

    struct PendingTransaction {
        quint8 transactionId;
        QUuid uuid;
        WatchConnection::Dict dict;
        std::function<void()> ackCallback;
        std::function<void()> nackCallback;
    };
    QQueue<PendingTransaction> _pending;
    QTimer *_timeout;
};

#endif // APPMSGMANAGER_H
