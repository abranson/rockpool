#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <QObject>
#include <QFlags>
#include <QDebug>

enum ConnectivityFlag {
    CONNECTED = 0b1,
    PAIRED = 0b10,
    ENCRYPTED = 0b100,
    HAS_BONDED_GATEWAY = 0b1000,
    STALE_PAIRING = 0b100000
};
Q_DECLARE_FLAGS(ConnectivityFlags, ConnectivityFlag);

class Connectivity : public QObject
{
public:
    Connectivity(const QByteArray &value);

    bool connected() const;
    bool paired() const;
    bool encrypted() const;
    bool hasBondedGateway() const;
    bool isUsingStalePairing() const;

public slots:
    void connectivityFlagsChanged(QByteArray value);

private:
    ConnectivityFlags m_flags;
    quint8 m_pairingError;
};

#endif // CONNECTIVITY_H