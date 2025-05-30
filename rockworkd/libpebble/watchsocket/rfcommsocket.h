#ifndef RFCOMMSOCKET_H
#define RFCOMMSOCKET_H

#include <QBluetoothSocket>
#include "watchsocket.h"

class RfcommSocket : public WatchSocket
{
public:
    RfcommSocket(QBluetoothAddress address);

    SocketType type() const override
    {
        return SocketType::RFCOMM;
    }

    SocketState state() const override
    {
        return m_state;
    }

    qint64 bytesAvailable() const override
    {
        return m_socket->bytesAvailable();
    }

    QByteArray read(qint64 maxSize) override
    {
        return m_socket->read(maxSize);
    }
    qint64 write(const QByteArray &data) override
    {
        return m_socket->write(data);
    }
    QByteArray peek(qint64 maxSize) override
    {
        return m_socket->peek(maxSize);
    }
    qint64 peek(char *data, qint64 maxSize) override
    {
        return m_socket->peek(data, maxSize);
    }

    void connect() override;
    void disconnect() override;

    void close() override {
        m_socket->close();
    }

private:
    SocketState m_state;

    QBluetoothSocket *m_socket;
    QBluetoothAddress m_address;
};
#endif // RFCOMMSOCKET_H