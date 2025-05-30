#include "rfcommsocket.h"
#define WATCH_UUID QString("00000000-deca-fade-deca-deafdecacaff")

RfcommSocket::RfcommSocket(QBluetoothAddress address):
  m_address(address),
  m_state(SocketState::DisconnectedState),
  m_socket(nullptr)
{
    QObject::connect(this, &WatchSocket::connected, [this]() {
        m_state = SocketState::ConnectedState;
    });
    QObject::connect(this, &WatchSocket::disconnected, [this]() {
        m_state = SocketState::DisconnectedState;
    });
}

void RfcommSocket::connect()
{
    if (m_socket) {
        disconnect();
    }

    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    QObject::connect(m_socket, &QBluetoothSocket::connected, this, &WatchSocket::connected);
    QObject::connect(m_socket, &QBluetoothSocket::disconnected, this, &WatchSocket::disconnected);
    QObject::connect(m_socket, &QBluetoothSocket::readyRead, this, &WatchSocket::readyRead);

    m_socket->connectToService(m_address, QBluetoothUuid(WATCH_UUID), QIODevice::ReadWrite);
}

void RfcommSocket::disconnect()
{
    m_state = SocketState::DisconnectedState;

    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();

        m_socket = nullptr;
    }
}