#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"
#include "uploadmanager.h"

#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>
#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>
#include <QBluetoothSocket>
#include <QtEndian>
#include <QDateTime>

WatchConnection::WatchConnection(QObject *parent) :
    QObject(parent),
    m_socket(nullptr)
{
    m_reconnectTimer.setSingleShot(true);
    QObject::connect(&m_reconnectTimer, &QTimer::timeout, this, &WatchConnection::reconnect);

    m_localDevice = new QBluetoothLocalDevice(this);
    connect(m_localDevice, &QBluetoothLocalDevice::hostModeStateChanged, this, &WatchConnection::hostModeStateChanged);

    m_uploadManager = new UploadManager(this, this);
}

UploadManager *WatchConnection::uploadManager() const
{
    return m_uploadManager;
}

void WatchConnection::scheduleReconnect()
{
    if (m_connectionAttempts == 0) {
        reconnect();
    } else if (m_connectionAttempts < 25) {
        qDebug() << "Attempting to reconnect in 10 seconds";
        m_reconnectTimer.start(1000 * 10);
    } else if (m_connectionAttempts < 35) {
        qDebug() << "Attempting to reconnect in 1 minute";
        m_reconnectTimer.start(1000 * 60);
    } else {
        qDebug() << "Attempting to reconnect in 15 minutes";
        m_reconnectTimer.start(1000 * 60 * 15);
    }
}

void WatchConnection::reconnect()
{
    QBluetoothLocalDevice localBtDev;
    if (localBtDev.pairingStatus(m_pebbleAddress) == QBluetoothLocalDevice::Unpaired) {
        // Try again in one 10 secs, give the user some time to pair it
        m_connectionAttempts = 1;
        scheduleReconnect();
        return;
    }

    if (m_socket) {
        if (m_socket->state() == QBluetoothSocket::ConnectedState) {
            qDebug() << "Already connected.";
            return;
        }
        delete m_socket;
    }

    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(m_socket, &QBluetoothSocket::connected, this, &WatchConnection::pebbleConnected);
    connect(m_socket, &QBluetoothSocket::readyRead, this, &WatchConnection::readyRead);
    connect(m_socket, SIGNAL(error(QBluetoothSocket::SocketError)), this, SLOT(socketError(QBluetoothSocket::SocketError)));
    connect(m_socket, &QBluetoothSocket::disconnected, this, &WatchConnection::pebbleDisconnected);
    //connect(socket, SIGNAL(bytesWritten(qint64)), SLOT(onBytesWritten(qint64)));

    m_connectionAttempts++;

    // FIXME: Assuming port 1 (with Pebble)
    m_socket->connectToService(m_pebbleAddress, 1);
}

void WatchConnection::connectPebble(const QBluetoothAddress &pebble)
{
    m_pebbleAddress = pebble;
    m_connectionAttempts = 0;
    scheduleReconnect();
}

bool WatchConnection::isConnected()
{
    return m_socket && m_socket->state() == QBluetoothSocket::ConnectedState;
}

void WatchConnection::writeToPebble(Endpoint endpoint, const QByteArray &data)
{
    if (!m_socket || m_socket->state() != QBluetoothSocket::ConnectedState) {
        qWarning() << "Socket not open. Cannot send data to Pebble. (Endpoint:" << endpoint << ")";
        return;
    }

    //qDebug() << "sending message to endpoint" << endpoint;
    QByteArray msg;

    msg.append((data.length() & 0xFF00) >> 8);
    msg.append(data.length() & 0xFF);

    msg.append((endpoint & 0xFF00) >> 8);
    msg.append(endpoint & 0xFF);

    msg.append(data);

    //qDebug() << "Writing:" << msg.toHex();
    m_socket->write(msg);
}

void WatchConnection::systemMessage(WatchConnection::SystemMessage msg)
{
    QByteArray data;
    data.append((char)0);
    data.append((char)msg);
    writeToPebble(EndpointSystemMessage, data);
}

bool WatchConnection::registerEndpointHandler(WatchConnection::Endpoint endpoint, QObject *handler, const QString &method)
{
    if (m_endpointHandlers.contains(endpoint)) {
        qWarning() << "Already have a handlder for endpoint" << endpoint;
        return false;
    }
    Callback cb;
    cb.obj = handler;
    cb.method = method;
    m_endpointHandlers.insert(endpoint, cb);
    return true;
}

void WatchConnection::pebbleConnected()
{
    m_connectionAttempts = 0;
    emit watchConnected();
}

void WatchConnection::pebbleDisconnected()
{
    qDebug() << "Disconnected";
    //m_socket->close();
    emit watchDisconnected();
    if (!m_reconnectTimer.isActive()) {
        scheduleReconnect();
    }
}

void WatchConnection::socketError(QBluetoothSocket::SocketError error)
{
    Q_UNUSED(error); // We seem to get UnknownError anyways all the time
    qDebug() << "SocketError" << error;
    m_socket->close();
    emit watchConnectionFailed();
    if (!m_reconnectTimer.isActive()) {
        scheduleReconnect();
    }
}

void WatchConnection::readyRead()
{
//    QByteArray data = m_socket->readAll();
//    qDebug() << "data from pebble" << data.toHex();

//    QByteArray header = data.left(4);
//    qDebug() << "header:" << header.toHex();
    if (!m_socket) {
        return;
    }
    int headerLength = 4;
    uchar header[4];
    m_socket->peek(reinterpret_cast<char*>(header), headerLength);

    quint16 messageLength = qFromBigEndian<quint16>(&header[0]);
    Endpoint endpoint = (Endpoint)qFromBigEndian<quint16>(&header[2]);

    if (m_socket->bytesAvailable() < headerLength + messageLength) {
//        qDebug() << "not enough data... waiting for more";
        return;
    }

    QByteArray data = m_socket->read(headerLength + messageLength);
//    qDebug() << "Have message for endpoint:" << endpoint << "data:" << data.toHex();

    data = data.right(data.length() - 4);

    if (m_endpointHandlers.contains(endpoint)) {
        if (m_endpointHandlers.contains(endpoint)) {
            Callback cb = m_endpointHandlers.value(endpoint);
            QMetaObject::invokeMethod(cb.obj.data(), cb.method.toLatin1(), Q_ARG(QByteArray, data));
        }
    } else {
        qWarning() << "Have message for unhandled endpoint" << endpoint << data.toHex();
    }

    if (m_socket->bytesAvailable() > 0) {
        readyRead();
    }
}

void WatchConnection::hostModeStateChanged(QBluetoothLocalDevice::HostMode state)
{
    switch (state) {
    case QBluetoothLocalDevice::HostPoweredOff:
        qDebug() << "Bluetooth turned off. Stopping any reconnect attempts.";
        m_reconnectTimer.stop();
        break;
    case QBluetoothLocalDevice::HostConnectable:
    case QBluetoothLocalDevice::HostDiscoverable:
    case QBluetoothLocalDevice::HostDiscoverableLimitedInquiry:
        if (m_socket && m_socket->state() != QBluetoothSocket::ConnectedState
                && m_socket->state() != QBluetoothSocket::ConnectingState
                && !m_reconnectTimer.isActive()) {
            qDebug() << "Bluetooth now active. Trying to reconnect";
            m_connectionAttempts = 0;
            scheduleReconnect();
        }
    }
}

QByteArray WatchConnection::buildData(QStringList data)
{
    QByteArray res;
    for (QString d : data)
    {
        QByteArray tmp = d.left(0xEF).toUtf8();
        res.append((tmp.length() + 1) & 0xFF);
        res.append(tmp);
        res.append('\0');
    }
    return res;
}

QByteArray WatchConnection::buildMessageData(uint lead, QStringList data)
{
    QByteArray res;
    res.append(lead & 0xFF);
    res.append(buildData(data));
    return res;
}
