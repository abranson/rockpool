#ifndef WATCHCONNECTION_H
#define WATCHCONNECTION_H

#include <QObject>
#include <QBluetoothAddress>
#include <QBluetoothSocket>
#include <QBluetoothLocalDevice>
#include <QtEndian>
#include <QPointer>
#include <QTimer>
#include <QFile>

class EndpointHandlerInterface;
class UploadManager;

class PebblePacket {
public:
    PebblePacket() {}
    virtual ~PebblePacket() = default;
    virtual QByteArray serialize() const = 0;
    virtual bool deserialize(const QByteArray &data) {qCritical() << "Attempt to deserialize using unimplemented method from" << data.toHex();return false;}
};

class Callback
{
public:
    QPointer<QObject> obj;
    QString method;
};

class WatchConnection : public QObject
{
    Q_OBJECT
public:

    enum Endpoint {
        EndpointUnknownEndpoint = 0,
        EndpointTime = 11,
        EndpointVersion = 16,
        EndpointPhoneVersion = 17,
        EndpointSystemMessage = 18,
        EndpointMusicControl = 32,
        EndpointPhoneControl = 33,
        EndpointApplicationMessage = 48,
        EndpointLauncher = 49,
        EndpointAppLaunch = 52,
        EndpointWatchLogs = 2000,
//        EndpointWatchPing = 2001,
        EndpointLogDump = 2002,
//        EndpointWatchReset = 2003,
//        EndpointWatchApp = 2004,
//        EndpointAppLogs = 2006,
        EndpointNotification = 3000,
//        watchEXTENSIBLE_NOTIFS = 3010, // Deprecated in 3.x
//        watchRESOURCE = 4000,
        EndpointFactorySettings = 5001,
        EndpointAppManager = 6000, // Deprecated in 3.x
        EndpointAppFetch = 6001, // New in 3.x
        EndpointDataLogging = 6778,
        EndpointScreenshot = 8000,
//        watchFILE_MANAGER = 8181,
//        watchCORE_DUMP = 9000,
        EndpointAudioStream = 10000, // New in 3.x
        EndpointVoiceControl = 11000,
        EndpointActionHandler = 11440,
        EndpointBlobDB = 0xB1DB, // New in 3.x
        EndpointNotify = 0xB2DB, // BlobDB Update Notify
        EndpointSorting = 0xabcd,
        EndpointPutBytes = 0xbeef
    };

    enum SystemMessage {
        SystemMessageFirmwareAvailable = 0,
        SystemMessageFirmwareStart = 1,
        SystemMessageFirmwareComplete = 2,
        SystemMessageFirmwareFail = 3,
        SystemMessageFirmwareUpToDate = 4,
        SystemMessageFirmwareOutOfDate = 5,
        SystemMessageBluetoothStartDiscoverable = 6,
        SystemMessageBluetoothEndDiscoverable = 7
    };

    typedef QMap<int, QVariant> Dict;
    enum DictItemType {
        DictItemTypeBytes,
        DictItemTypeString,
        DictItemTypeUInt,
        DictItemTypeInt
    };

    enum UploadType {
        UploadTypeFirmware = 1,
        UploadTypeRecovery = 2,
        UploadTypeSystemResources = 3,
        UploadTypeResources = 4,
        UploadTypeBinary = 5,
        UploadTypeFile = 6,
        UploadTypeWorker = 7
    };
    enum UploadStatus {
        UploadStatusProgress,
        UploadStatusFailed,
        UploadStatusSuccess
    };

    explicit WatchConnection(QObject *parent = 0);
    UploadManager *uploadManager() const;

    void connectPebble(const QBluetoothAddress &pebble);
    bool isConnected();

    QByteArray buildData(QStringList data);
    QByteArray buildMessageData(uint lead, QStringList data);

    void writeRawData(const QByteArray &data);
    void writeToPebble(Endpoint endpoint, const QByteArray &data);
    void systemMessage(SystemMessage msg);

    bool registerEndpointHandler(Endpoint endpoint, QObject *handler, const QString &method);

signals:
    void watchConnected();
    void watchDisconnected();
    void watchConnectionFailed();

    void rawOutgoingMsg(QByteArray &msg);
    void rawIncomingMsg(QByteArray &msg);

private:
    void scheduleReconnect();
    void reconnect();

private slots:
    void hostModeStateChanged(QBluetoothLocalDevice::HostMode state);
    void pebbleConnected();
    void pebbleDisconnected();
    void socketError(QBluetoothSocket::SocketError error);
    void readyRead();
//    void logData(const QByteArray &data);


private:
    QBluetoothAddress m_pebbleAddress;
    QBluetoothLocalDevice *m_localDevice;
    QBluetoothSocket *m_socket = nullptr;
    int m_connectionAttempts = 0;
    QTimer m_reconnectTimer;

    UploadManager *m_uploadManager;
    QHash<Endpoint, Callback> m_endpointHandlers;
};

#endif // WATCHCONNECTION_H
