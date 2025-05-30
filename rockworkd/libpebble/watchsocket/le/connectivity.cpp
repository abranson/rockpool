#include "connectivity.h"

Connectivity::Connectivity(const QByteArray &value)
{
    qDebug() << "We're in conenctivity constructor" << value.toHex();
    connectivityFlagsChanged(value);
}

bool Connectivity::connected() const
{
    return m_flags & CONNECTED;
}

bool Connectivity::paired() const
{
    return m_flags & PAIRED;
}

bool Connectivity::encrypted() const
{
    return m_flags & ENCRYPTED;
}

bool Connectivity::hasBondedGateway() const
{
    return m_flags & HAS_BONDED_GATEWAY;
}

bool Connectivity::isUsingStalePairing() const
{
    return m_flags & STALE_PAIRING;
}

void Connectivity::connectivityFlagsChanged(QByteArray value)
{
    if (value.length() > 0) {
        quint8 flags = value.at(0);
        m_flags = (ConnectivityFlags)flags;
        qDebug() << "Pairing error is" << (quint8)value.at(3);
        m_pairingError = (quint8)value.at(3);
        qDebug() << "Checking connectivity flags!" << connected() << paired() << encrypted() << hasBondedGateway() << isUsingStalePairing();
    }
}