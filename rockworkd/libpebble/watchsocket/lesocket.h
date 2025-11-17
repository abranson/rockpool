#ifndef LE_SOCKET_H
#define LE_SOCKET_H

#include "watchsocket.h"

#include "le/gattserver.h"
#include "le/packetreader.h"
#include <QBluetoothAddress>

class Device;
class Connectivity;

const QString mtuCharacteristic = "00000003-328e-0fbb-c642-1aa6699bdada";
const QString pairingCharacteristic = "00000002-328e-0fbb-c642-1aa6699bdada";
const QString connectivityCharacteristic = "00000001-328e-0fbb-c642-1aa6699bdada";

class LESocket : public WatchSocket
{
public:
    LESocket(const QBluetoothDeviceInfo &address, Device *device);
    ~LESocket();

    SocketType type() const override
    {
        return SocketType::LE;
    }

    SocketState state() const override
    {
        return m_state;
    }

    qint64 bytesAvailable() const override
    {
        return m_reader->bytesAvailable();
    }

    QByteArray read(qint64 maxSize) override
    {
        return m_reader->read(maxSize);
    }
    qint64 write(const QByteArray &data) override
    {
        m_server->writeToPebble(data);
        return data.length();
    }
    QByteArray peek(qint64 maxSize) override
    {
        return m_reader->peek(maxSize);
    }
    qint64 peek(char *data, qint64 maxSize) override
    {
        return m_reader->peek(data, maxSize);
    }

    void connect() override;
    void disconnect() override;

    void close() override;

private slots:
    void handleConnectedChanged();
    void pairToPebble(DeviceService *service);

private:
QByteArray pairTriggers(bool supportsPinningWithoutSlaveSecurity, bool belowLollipop, bool clientMode);

private slots:
    void connectionParamsChanged(const QByteArray &value);

template <std::size_t N>
QByteArray boolArrayToBytes(std::array<bool, N> &arr) {
    QByteArray ret;
    ret.reserve((arr.size() + 7) / 8);
    ret.resize((arr.size() + 7) / 8);
    for (int i = 0; i < arr.size(); i++) {
        int i2 = i / 8;
        int i3 = i % 8;
        if (arr.at(i)) {
            ret[i2] = (quint8) ((1 << i3) | ret[i2]);
        }
    }
    return ret;
}

    SocketState m_state;

    Device *m_device;

    GATTServer *m_server;
    PacketReader *m_reader;
    Connectivity *m_connectivity;
};
#endif // LE_SOCKET_H
