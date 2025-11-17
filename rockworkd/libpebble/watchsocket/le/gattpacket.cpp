#include "gattpacket.h"

GATTPacket::GATTPacket():
  m_gattVersion(PPoGATTVersion::ZERO)
{
}

GATTPacket::GATTPacket(QByteArray data):
  m_gattVersion(PPoGATTVersion::ZERO)
{
    m_data = data;
    quint8 header = data.at(0);
    m_sequence = ((header & sequenceMask) >> 3);
    if (m_sequence < 0 || m_sequence > 31) {
        qDebug() << "Error: sequence invalid";
        return;
    }
    m_type = (PacketType) (header & typeMask);

    if (m_type == GATTPacket::RESET) {
        PPoGATTVersion::GATTVersion version = (PPoGATTVersion::GATTVersion)data.at(1);
        m_gattVersion = PPoGATTVersion(version);
    }
}

GATTPacket::GATTPacket(PacketType type, int sequence, QByteArray data):
  m_gattVersion(PPoGATTVersion::ZERO)
{
    m_sequence = sequence;
    m_type = type;

    if (!data.isEmpty() && !data.isNull()) {
        m_data = QByteArray();
        m_data.reserve(data.length() + 1);
    } else {
        m_data = QByteArray();
    }

    m_data.append((type | (((sequence << 3) & sequenceMask))));
    if (!data.isEmpty() && !data.isNull()) {
        m_data.append(data);
    }
}

GATTPacket::~GATTPacket()
{
    m_data.clear();
}

int GATTPacket::getMaxRxWindow() const
{
    if (m_type == GATTPacket::RESET_ACK) {
        return m_data[1];
    }

    return 0; // TODO: ales??
}

int GATTPacket::getMaxTxWindow() const
{
    if (m_type == GATTPacket::RESET_ACK) {
        return m_data[2];
    }

    return 0; // TODO: ales??
}

GATTPacket::PacketType GATTPacket::type() const
{
    return m_type;
}

int GATTPacket::sequence() const
{
    return m_sequence;
}

PPoGATTVersion GATTPacket::version() const
{
    return m_gattVersion;
}

QByteArray GATTPacket::data() const
{
    return m_data;
}
