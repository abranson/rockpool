#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"
#include "uploadmanager.h"

#include "bluez/bluezclient.h"

#include "watchsocket/rfcommsocket.h"
#include "watchsocket/lesocket.h"

#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>
#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>
#include <QBluetoothSocket>
#include <QtEndian>
#include <QDateTime>

// TODO: Maybe only get the Device class from Pebble, instead of BluezClient?
WatchConnection::WatchConnection(BluezClient *client, QObject *parent):
  QObject(parent),
  m_client(client),
  m_socket(nullptr)
{
    m_reconnectTimer.setSingleShot(true);
    QObject::connect(&m_reconnectTimer, &QTimer::timeout, this, &WatchConnection::reconnect);

    m_localDevice = new QBluetoothLocalDevice(this);
    connect(m_localDevice, &QBluetoothLocalDevice::hostModeStateChanged, this, &WatchConnection::hostModeStateChanged);

    m_uploadManager = new UploadManager(this, this);

    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &WatchConnection::pebbleDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, [this]() {
        qWarning() << "Discovery finished, reconnecting...";
        scheduleReconnect();
    });
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
        qDebug() << "Attempting to reconnect in 5 minutes";
        m_reconnectTimer.start(1000 * 60 * 5);
    }
}

void WatchConnection::reconnect()
{
    QBluetoothLocalDevice localBtDev;
    if (localBtDev.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        qDebug() << "Bluetooth powered off. Ceasing connection attempts";
        if (m_reconnectTimer.isActive()) m_reconnectTimer.stop();
        return;
    }
    /*if (localBtDev.pairingStatus(m_pebbleAddress) == QBluetoothLocalDevice::Unpaired) {
        // Try again in one 10 secs, give the user some time to pair it
        m_connectionAttempts = 1;
        scheduleReconnect();
        return;
    }*/

    if (m_socket) {
        if (m_socket->state() == WatchSocket::ConnectedState) {
            qDebug() << "Already connected.";
            return;
        }
    }

    m_connectionAttempts++;

    qWarning() << "Attempting to reconnect," << m_connectionAttempts << m_discoveryAgent->isActive();

    if (!m_discoveryAgent->isActive())
        m_discoveryAgent->start();
}

void WatchConnection::connectPebble(const QBluetoothAddress &pebble)
{
    m_pebbleAddress = pebble;
    m_connectionAttempts = 0;
    scheduleReconnect();
}

bool WatchConnection::isConnected()
{
    return m_socket && m_socket->state() == WatchSocket::ConnectedState;
}

void WatchConnection::writeToPebble(Endpoint endpoint, const QByteArray &data)
{
    if (!m_socket || m_socket->state() != WatchSocket::ConnectedState) {
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
    writeRawData(msg);
    emit rawOutgoingMsg(msg);
}

void WatchConnection::writeRawData(const QByteArray &msg)
{
    //qDebug() << "Writing:" << msg.toHex();
    m_socket->write(msg);
}

void WatchConnection::systemMessage(WatchConnection::SystemMessage msg)
{
    systemMessage(msg, QByteArray());
}

void WatchConnection::systemMessage(WatchConnection::SystemMessage msg, const QByteArray &data)
{
    QByteArray output;
    output.append((char)0);
    output.append((char)msg);

    if (!data.isEmpty() && !data.isNull())
        output.append(data);

    writeToPebble(EndpointSystemMessage, output);
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
    if (m_reconnectTimer.isActive())
        m_reconnectTimer.stop();

    emit watchConnected();
}

void WatchConnection::pebbleDisconnected()
{
    qDebug() << "Disconnected";
    emit watchDisconnected();
    if (!m_reconnectTimer.isActive()) {
        scheduleReconnect();
    }
}

void WatchConnection::socketError(QBluetoothSocket::SocketError error)
{
    Q_UNUSED(error); // We seem to get UnknownError anyways all the time
    qWarning() << "SocketError" << error;
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
    emit rawIncomingMsg(data);

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
    qDebug() << "Bluetooth host changed state:" << state;
    if (state == QBluetoothLocalDevice::HostPoweredOff)
    {
        qDebug() << "Bluetooth turned off. Stopping any reconnect attempts.";
        if (m_socket) {
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
        }

        m_reconnectTimer.stop();
    }
    else if (!isConnected() && !m_reconnectTimer.isActive())
    {
        qDebug() << "Bluetooth now active. Trying to reconnect";
        m_connectionAttempts = 0;
        scheduleReconnect();
    }
}

void WatchConnection::pebbleDiscovered(const QBluetoothDeviceInfo &device)
{
    if (device.address() == m_pebbleAddress) {
        qWarning() << "Pebble discovered!" << device.serviceUuids() << device.rssi();
        if (device.serviceUuids().isEmpty())
            return;

        if (m_socket) {
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
        }

        // TODO: Stop destroying and creating the socket over and over again
        if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
            m_socket = new LESocket(device, m_client->getDevice(m_pebbleAddress));
        else
            m_socket = new RfcommSocket(m_pebbleAddress);

        connect(m_socket, &WatchSocket::connected, this, &WatchConnection::pebbleConnected);
        connect(m_socket, &WatchSocket::readyRead, this, &WatchConnection::readyRead);
        //connect(m_socket, SIGNAL(error(QBluetoothSocket::SocketError)), this, SLOT(socketError(QBluetoothSocket::SocketError)));
        connect(m_socket, &WatchSocket::disconnected, this, &WatchConnection::pebbleDisconnected);

        m_socket->connect();
        m_discoveryAgent->stop();
    }
}