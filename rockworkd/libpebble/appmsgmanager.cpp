#include <QTimer>

#include "pebble.h"
#include "appmsgmanager.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

// TODO D-Bus server for non JS kit apps!!!!

AppMsgManager::AppMsgManager(Pebble *pebble, AppManager *apps, WatchConnection *connection)
    : QObject(pebble),
      m_pebble(pebble),
      apps(apps),
      m_connection(connection),
      _lastTransactionId(0),
      m_currentUuid(QUuid()),
      _timeout(new QTimer(this))
{
    connect(m_connection, &WatchConnection::watchConnected,
            this, &AppMsgManager::handleWatchConnectedChanged);
    connect(m_connection, &WatchConnection::watchDisconnected,
            this, &AppMsgManager::handleWatchConnectedChanged);
    connect(m_pebble, &Pebble::pebbleConnected,
            this, &AppMsgManager::handlePebbleConnected);

    _timeout->setSingleShot(true);
    _timeout->setInterval(3000);
    connect(_timeout, &QTimer::timeout,
            this, &AppMsgManager::handleTimeout);

    m_connection->registerEndpointHandler(WatchConnection::EndpointLauncher, this, "handleLauncherMessage");
    m_connection->registerEndpointHandler(WatchConnection::EndpointAppLaunch, this, "handleAppLaunchMessage");
    m_connection->registerEndpointHandler(WatchConnection::EndpointApplicationMessage, this, "handleApplicationMessage");
}

void AppMsgManager::handleLauncherMessage(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint8 messageType = reader.read<quint8>();
    switch (messageType) {
    case AppMessagePush:
        handleLauncherPushMessage(data);
        break;

    // TODO we ignore those for now.
    case AppMessageAck:
        qDebug() << "Watch accepted application launch";
        break;
    case AppMessageNack:
        qDebug() << "Watch denied application launch";
        break;
    case AppMessageRequest:
        qWarning() << "Unhandled Launcher message (AppMessagePush)";
        break;
    }
}

void AppMsgManager::handleApplicationMessage(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint8 messageType = reader.read<quint8>();
    switch (messageType) {
    case AppMessagePush:
        handlePushMessage(data);
        break;
    case AppMessageAck:
        handleAckMessage(data, true);
        break;
    case AppMessageNack:
        handleAckMessage(data, false);
        break;
    default:
        qWarning() << "Unknown application message type:" << int(data.at(0));
        break;
    }
}

void AppMsgManager::send(const QUuid &uuid, const QVariantMap &data, const std::function<void ()> &ackCallback, const std::function<void ()> &nackCallback)
{
    PendingTransaction trans;
    trans.uuid = uuid;
    trans.transactionId = ++_lastTransactionId;
    //TODO check for byte arrays and byte arrays with strings (https://developer.pebble.com/guides/pebble-apps/pebblekit-js/js-app-comm/#appmessage-objects-in-javascript)
    trans.dict = mapAppKeys(uuid, data);
    trans.ackCallback = ackCallback;
    trans.nackCallback = nackCallback;

    qDebug() << "Queueing appmsg" << trans.transactionId << "to" << trans.uuid
                      << "with dict" << trans.dict;

    _pending.enqueue(trans);
    if (_pending.size() == 1) {
        // This is the only transaction on the queue
        // Therefore, we were idle before: we can submit this transaction right now.
        transmitNextPendingTransaction();
    }
}

void AppMsgManager::setMessageHandler(const QUuid &uuid, MessageHandlerFunc func)
{
    _handlers.insert(uuid, func);
}

void AppMsgManager::clearMessageHandler(const QUuid &uuid)
{
    _handlers.remove(uuid);
}

uint AppMsgManager::lastTransactionId() const
{
    return _lastTransactionId;
}

uint AppMsgManager::nextTransactionId() const
{
    return _lastTransactionId + 1;
}

void AppMsgManager::send(const QUuid &uuid, const QVariantMap &data)
{
    std::function<void()> nullCallback;
    send(uuid, data, nullCallback, nullCallback);
}

void AppMsgManager::launchApp(const QUuid &uuid)
{
    if (m_pebble->softwareVersion() < "v3.0") {
        WatchConnection::Dict dict;
        dict.insert(1, LauncherActionStart);

        qDebug() << "Sending start message to launcher" << uuid << dict;
        QByteArray msg = buildPushMessage(++_lastTransactionId, uuid, dict);
        m_connection->writeToPebble(WatchConnection::EndpointLauncher, msg);
    }
    else {
        QByteArray msg = buildLaunchMessage(LauncherActionStart, uuid);
        qDebug() << "Sending start message to launcher" << uuid;
        m_connection->writeToPebble(WatchConnection::EndpointAppLaunch, msg);
    }
}

void AppMsgManager::closeApp(const QUuid &uuid)
{
    if (m_pebble->softwareVersion() < "v3.0") {
        WatchConnection::Dict dict;
        dict.insert(1, LauncherActionStop);

        qDebug() << "Sending stop message to launcher" << uuid << dict;
        QByteArray msg = buildPushMessage(++_lastTransactionId, uuid, dict);
        m_connection->writeToPebble(WatchConnection::EndpointLauncher, msg);
    }
    else {
        QByteArray msg = buildLaunchMessage(LauncherActionStop, uuid);
        qDebug() << "Sending stop message to launcher" << uuid;
        m_connection->writeToPebble(WatchConnection::EndpointAppLaunch, msg);
    }
}

WatchConnection::Dict AppMsgManager::mapAppKeys(const QUuid &uuid, const QVariantMap &data)
{
    AppInfo info = apps->info(uuid);
    if (info.uuid() != uuid) {
        qWarning() << "Unknown app GUID while sending message:" << uuid;
    }

    WatchConnection::Dict d;

    qDebug() << "Have appkeys:" << info.appKeys().keys();

    for (QVariantMap::const_iterator it = data.constBegin(); it != data.constEnd(); ++it) {
        if (info.appKeys().contains(it.key())) {
            d.insert(info.appKeys().value(it.key()), it.value());
        } else {
            // Even if we do not know about this appkey, try to see if it's already a numeric key we
            // can send to the watch.
            bool ok = false;
            int num = it.key().toInt(&ok);
            if (ok) {
                d.insert(num, it.value());
            } else {
                qWarning() << "Unknown appKey" << it.key() << "for app with GUID" << uuid;
            }
        }
    }

    return d;
}

QVariantMap AppMsgManager::mapAppKeys(const QUuid &uuid, const WatchConnection::Dict &dict)
{
    AppInfo info = apps->info(uuid);
    if (info.uuid() != uuid) {
        qWarning() << "Unknown app GUID while sending message:" << uuid;
    }

    QVariantMap data;

    for (WatchConnection::Dict::const_iterator it = dict.constBegin(); it != dict.constEnd(); ++it) {
        qDebug() << "checking app key" << it.key() << info.appKeys().key(it.key());
        if (info.appKeys().values().contains(it.key())) {
            data.insert(info.appKeys().key(it.key()), it.value());
        } else {
            qWarning() << "Unknown appKey value" << it.key() << "for app with GUID" << uuid;
            data.insert(QString::number(it.key()), it.value());
        }
    }

    return data;
}

bool AppMsgManager::unpackAppLaunchMessage(const QByteArray &msg, QUuid *uuid)
{
    WatchDataReader reader(msg);
    quint8 action = reader.read<quint8>();
    Q_UNUSED(action);

    *uuid = reader.readUuid();

    if (reader.bad()) {
        return false;
    }

    return true;
}

bool AppMsgManager::unpackPushMessage(const QByteArray &msg, quint8 *transaction, QUuid *uuid, WatchConnection::Dict *dict)
{
    WatchDataReader reader(msg);
    quint8 code = reader.read<quint8>();
    Q_UNUSED(code);
    Q_ASSERT(code == AppMessagePush);

    *transaction = reader.read<quint8>();
    *uuid = reader.readUuid();
    *dict = reader.readDict();

    if (reader.bad()) {
        return false;
    }

    return true;
}

QByteArray AppMsgManager::buildPushMessage(quint8 transaction, const QUuid &uuid, const WatchConnection::Dict &dict)
{
    QByteArray ba;
    WatchDataWriter writer(&ba);
    writer.write<quint8>(AppMessagePush);
    writer.write<quint8>(transaction);
    writer.writeUuid(uuid);
    writer.writeDict(dict);

    return ba;
}

QByteArray AppMsgManager::buildLaunchMessage(quint8 messageType, const QUuid &uuid)
{
    QByteArray ba;
    WatchDataWriter writer(&ba);
    writer.write<quint8>(messageType);
    if (!uuid.isNull()) {
        writer.writeUuid(uuid);
    }

    return ba;
}

QByteArray AppMsgManager::buildAckMessage(quint8 transaction)
{
    QByteArray ba(2, Qt::Uninitialized);
    ba[0] = AppMessageAck;
    ba[1] = transaction;
    return ba;
}

QByteArray AppMsgManager::buildNackMessage(quint8 transaction)
{
    QByteArray ba(2, Qt::Uninitialized);
    ba[0] = AppMessageNack;
    ba[1] = transaction;
    return ba;
}

void AppMsgManager::handleAppLaunchMessage(const QByteArray &data)
{
    QUuid uuid;
    if (!unpackAppLaunchMessage(data, &uuid)) {
        qWarning() << "Failed to parse App Launch message";
        return;
    }

    switch (data.at(0)) {
    case LauncherActionStart:
        qDebug() << "App starting in watch:" << uuid;
        m_currentUuid = uuid;
        emit appStarted(uuid);
        break;
    case LauncherActionStop:
        qDebug() << "App stopping in watch:" << uuid;
        emit appStopped(uuid);
        break;
    default:
        qWarning() << "App Launch pushed unknown message:" << uuid;
        break;
    }
}

void AppMsgManager::handleLauncherPushMessage(const QByteArray &data)
{
    quint8 transaction;
    QUuid uuid;
    WatchConnection::Dict dict;

    if (!unpackPushMessage(data, &transaction, &uuid, &dict)) {
        // Failed to parse!
        // Since we're the only one handling this endpoint,
        // all messages must be accepted
        qWarning() << "Failed to parser LAUNCHER PUSH message";
        return;
    }
    qDebug() << "have launcher push message" << data.toHex() << dict.keys();
    if (!dict.contains(1)) {
        qWarning() << "LAUNCHER message has no item in dict";
        return;
    }

    switch (dict.value(1).toInt()) {
    case LauncherActionStart:
        qDebug() << "App starting in watch:" << uuid;
        m_connection->writeToPebble(WatchConnection::EndpointLauncher, buildAckMessage(transaction));
        m_currentUuid = uuid;
        emit appStarted(uuid);
        break;
    case LauncherActionStop:
        qDebug() << "App stopping in watch:" << uuid;
        m_connection->writeToPebble(WatchConnection::EndpointLauncher, buildAckMessage(transaction));
        emit appStopped(uuid);
        break;
    default:
        qWarning() << "LAUNCHER pushed unknown message:" << uuid << dict;
        m_connection->writeToPebble(WatchConnection::EndpointLauncher, buildNackMessage(transaction));
        break;
    }
}

void AppMsgManager::handlePushMessage(const QByteArray &data)
{
    quint8 transaction;
    QUuid uuid;
    WatchConnection::Dict dict;

    if (!unpackPushMessage(data, &transaction, &uuid, &dict)) {
        qWarning() << "Failed to parse APP_MSG PUSH";
        m_connection->writeToPebble(WatchConnection::EndpointApplicationMessage, buildNackMessage(transaction));
        return;
    }

    qDebug() << "Received appmsg PUSH from" << uuid << "with" << dict;

    QVariantMap msg = mapAppKeys(uuid, dict);
    qDebug() << "Mapped dict" << msg;

    bool result;

    MessageHandlerFunc handler = _handlers.value(uuid);
    if (handler) {
        result = handler(msg);
    } else {
        // No handler? Let's just send an ACK.
        result = false;
    }

    if (result) {
        qDebug() << "ACKing transaction" << transaction;
        m_connection->writeToPebble(WatchConnection::EndpointApplicationMessage, buildAckMessage(transaction));
    } else {
        qDebug() << "NACKing transaction" << transaction;
        m_connection->writeToPebble(WatchConnection::EndpointApplicationMessage, buildNackMessage(transaction));
    }
}

void AppMsgManager::handleAckMessage(const QByteArray &data, bool ack)
{
    if (data.size() < 2) {
        qWarning() << "invalid ack/nack message size";
        return;
    }

    const quint8 type = data[0]; Q_UNUSED(type);
    const quint8 recv_transaction = data[1];

    Q_ASSERT(type == AppMessageAck || type == AppMessageNack);

    if (_pending.empty()) {
        qWarning() << "received an ack/nack for transaction" << recv_transaction << "but no transaction is pending";
        return;
    }

    PendingTransaction &trans = _pending.head();
    if (trans.transactionId != recv_transaction) {
        qWarning() << "received an ack/nack but for the wrong transaction";
    }

    qDebug() << "Got " << (ack ? "ACK" : "NACK") << " to transaction" << trans.transactionId;

    _timeout->stop();

    if (ack) {
        if (trans.ackCallback) {
            trans.ackCallback();
        }
    } else {
        if (trans.nackCallback) {
            trans.nackCallback();
        }
    }

    _pending.dequeue();

    if (!_pending.empty()) {
        transmitNextPendingTransaction();
    }
}

void AppMsgManager::handleWatchConnectedChanged()
{
    if (!m_connection->isConnected()) {
        emit appStopped(m_currentUuid);

        // If the watch is disconnected, everything breaks loose
        // TODO In the future we may want to avoid doing the following.

        abortPendingTransactions();
    }
}

void AppMsgManager::handlePebbleConnected()
{
    //Now that we have all the info from the pebble "relaunch" the current app
    emit appStarted(m_currentUuid);
}

void AppMsgManager::handleTimeout()
{
    // Abort the first transaction
    Q_ASSERT(!_pending.empty());
    PendingTransaction trans = _pending.dequeue();

    qWarning() << "timeout on appmsg transaction" << trans.transactionId;

    if (trans.nackCallback) {
        trans.nackCallback();
    }

    if (!_pending.empty()) {
        transmitNextPendingTransaction();
    }
}

void AppMsgManager::transmitNextPendingTransaction()
{
    Q_ASSERT(!_pending.empty());
    PendingTransaction &trans = _pending.head();

    QByteArray msg = buildPushMessage(trans.transactionId, trans.uuid, trans.dict);

    m_connection->writeToPebble(WatchConnection::EndpointApplicationMessage, msg);

    _timeout->start();
}

void AppMsgManager::abortPendingTransactions()
{
    // Invoke all the NACK callbacks in the pending queue, then drop them.
    Q_FOREACH(const PendingTransaction &trans, _pending) {
        if (trans.nackCallback) {
            trans.nackCallback();
        }
    }

    _pending.clear();
}
