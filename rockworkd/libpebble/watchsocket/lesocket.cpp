#include "lesocket.h"
#include "le/connectivity.h"

#include "../bluez/device/device.h"

LESocket::LESocket(const QBluetoothDeviceInfo &address, Device *device):
  m_device(device)
{
    m_reader = new PacketReader();
    m_server = new GATTServer(m_reader, this);
    QObject::connect(m_server, &GATTServer::connectedToPebble, this, &WatchSocket::connected);
    QObject::connect(m_server, &GATTServer::disconnectedFromPebble, this, &WatchSocket::disconnected);
    QObject::connect(m_reader, &PacketReader::readyRead, this, &WatchSocket::readyRead);
    m_server->run();

    QObject::connect(this, &WatchSocket::connected, [this]() {
        m_state = SocketState::ConnectedState;
    });
    QObject::connect(this, &WatchSocket::disconnected, [this]() {
        m_state = SocketState::DisconnectedState;
    });

    QObject::connect(device, &Device::connectedChanged, this, &LESocket::handleConnectedChanged);
}

LESocket::~LESocket()
{
    m_reader->deleteLater();
    QObject::disconnect(m_device);
}

void LESocket::connect()
{
    m_device->connectToDevice();
}

void LESocket::disconnect()
{
    m_device->disconnectFromDevice();
}

void LESocket::close()
{
    qWarning() << "TODO: implement close";
}

void LESocket::handleConnectedChanged()
{
    if (m_device->connected())
        m_device->getService("0000fed9-0000-1000-8000-00805f9b34fb", this, &LESocket::pairToPebble);
}

void LESocket::pairToPebble(DeviceService *service)
{
    DeviceCharacteristic* characteristic = service->characteristic(pairingCharacteristic);
    DeviceCharacteristic *connChar = service->characteristic("00000005-328E-0FBB-C642-1AA6699BDADA");

    QByteArray conS;
    conS.append((char)0);
    conS.append(1);
    connChar->writeCharacteristic(conS);
    connChar->subscribeToCharacteristic(this, &LESocket::connectionParamsChanged);
    m_connectivity = new Connectivity(service->characteristic(connectivityCharacteristic)->readCharacteristic());
    service->characteristic(connectivityCharacteristic)->subscribeToCharacteristic(m_connectivity, &Connectivity::connectivityFlagsChanged);

    if (!m_device->paired() && !m_connectivity->paired()) {
        QByteArray data = pairTriggers(true, false, false);

        // Trigger pair on watch
        characteristic->writeCharacteristic(data);

        // Trigger pair from our side
        m_device->pair();
    }
}

void LESocket::connectionParamsChanged(const QByteArray &value)
{
    qWarning() << "Connection params changed!" << value.toHex();
}

QByteArray LESocket::pairTriggers(bool supportsPinningWithoutSlaveSecurity, bool belowLollipop, bool clientMode)
{
    std::array<bool, 6> boolArray = { true, supportsPinningWithoutSlaveSecurity, false, belowLollipop, clientMode };
    QByteArray ret = boolArrayToBytes(boolArray);
    return ret;
}