#ifndef GATTPACKET_H
#define GATTPACKET_H

#include <QObject>
#include <QDebug>

class PPoGATTVersion {
public:
    enum GATTVersion {
        ZERO = 0,
        ONE = 1
    };

    PPoGATTVersion() {};

    PPoGATTVersion(GATTVersion gattVersion) {
        version = gattVersion;
        if (version == ZERO) {
            supportsWindowNegotiation = false;
            supportsCoalescedAcking = false;
        } else if (version == ONE) {
            supportsWindowNegotiation = true;
            supportsCoalescedAcking = true;
        }
    }

    GATTVersion version = ZERO;
    bool supportsWindowNegotiation = false;
    bool supportsCoalescedAcking = false;
};

class GATTPacket {
public:
    enum PacketType {
        DATA = 0,
        ACK = 1,
        RESET = 2,
        RESET_ACK = 3
    };

    GATTPacket();
    GATTPacket(QByteArray data);
    GATTPacket(PacketType type, int sequence, QByteArray data);

    ~GATTPacket();

    int getMaxRxWindow() const;
    int getMaxTxWindow() const;

    PacketType type() const;
    int sequence() const;
    PPoGATTVersion version() const;
    QByteArray data() const;

private:
    quint8 typeMask = 0b111;
    quint8 sequenceMask = 0b11111000;

    PPoGATTVersion m_gattVersion;
    PacketType m_type;
    int m_sequence;
    QByteArray m_data;
};

#endif // GATTPACKET_H