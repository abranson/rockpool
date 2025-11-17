#ifndef WATCHSOCKET_H
#define WATCHSOCKET_H

#include <QObject>

enum SocketType {
    RFCOMM,
    LE
};

class DeviceService;

// TODO: Add methods to pair watch from inside the app
class WatchSocket : public QObject {
    Q_OBJECT

public:
    enum SocketState {
        ConnectedState,
        DisconnectedState
    };
    virtual SocketType type() const = 0;
    virtual SocketState state() const = 0;

    virtual qint64 bytesAvailable() const = 0;

    virtual QByteArray read(qint64 maxSize) = 0;
    virtual qint64 write(const QByteArray &data) = 0;
    virtual QByteArray peek(qint64 maxSize) = 0;
    virtual qint64 peek(char *data, qint64 maxSize) = 0;

    virtual void connect() = 0;
    virtual void disconnect() = 0;

    virtual void close() = 0;

signals:
    void connected();
    void disconnected();
    void readyRead();
};

#endif // WATCHSOCKET_H
