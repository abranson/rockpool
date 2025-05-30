#include <QObject>
#include <QThread>
#include <QTimer>

#include <QLowEnergyAdvertisingData>
#include <QLowEnergyAdvertisingParameters>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyCharacteristicData>
#include <QLowEnergyDescriptorData>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyServiceData>

#include "gattpacket.h"

class PacketReader;

const QBluetoothUuid serviceUuid = QBluetoothUuid(QString("10000000-328E-0FBB-C642-1AA6699BDADA"));
const QBluetoothUuid writeCharacteristic = QBluetoothUuid(QString("10000001-328E-0FBB-C642-1AA6699BDADA"));
const QBluetoothUuid readCharacteristic = QBluetoothUuid(QString("10000002-328E-0FBB-C642-1AA6699BDADA"));

const QBluetoothUuid fakeServiceUuid = QBluetoothUuid(QString("BADBADBA-DBAD-BADB-ADBA-BADBADBADBAD"));

const int default_maxRxWindow = 25;
const int default_maxTxWindow = 25;

class GATTServer : public QObject
{
    Q_OBJECT

public:
    GATTServer(PacketReader *reader, QObject *parent = nullptr);

    void sendDataToPebble();

public slots:
    void run();
    void sendAck(int sequence, bool forceAck = false);
    void writeToPebble(const QByteArray &data);

signals:
    void connectedToPebble();
    void disconnectedFromPebble();
    void dataReceived(QByteArray data);

private:
    const int m_maxPacketSize = 335;

    int m_sequence = 0;
    int m_remoteSequence = 0;

    int m_ackSequence = 0;

    inline int getNextSequence(int sequence) {
        return (sequence + 1) % 32;
    }

    bool m_connectionOpened = false;

    int m_maxRxWindow = default_maxRxWindow;
    int m_maxTxWindow = default_maxTxWindow;

    QList<QByteArray> m_pendingPackets;
    QList<GATTPacket> m_pendingAcks;
    int m_sentPackets = 0;

    GATTPacket m_lastAck;
    PPoGATTVersion m_gattVersion;

    int m_rxPending = 0;

    QTimer *m_timer;

    PacketReader *m_packetReader;

    QLowEnergyController *m_leController;
    QLowEnergyService *m_service, *m_fakeService;
};