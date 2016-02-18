#ifndef UPLOADMANAGER_H
#define UPLOADMANAGER_H

#include <functional>
#include <QQueue>
#include "watchconnection.h"

class UploadManager : public QObject
{
    Q_OBJECT

public:
    explicit UploadManager(WatchConnection *watch, QObject *parent = 0);

    typedef std::function<void()> SuccessCallback;
    typedef std::function<void(int)> ErrorCallback;
    typedef std::function<void(qreal)> ProgressCallback;

    uint upload(WatchConnection::UploadType type, int index, quint32 appInstallId, const QString &filename, int size = -1, quint32 crc = 0,
                SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());

    uint uploadAppBinary(quint32 appInstallId, const QString &filename, quint32 crc, SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());
    uint uploadAppResources(quint32 appInstallId, const QString &filename, quint32 crc, SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());
    uint uploadAppWorker(quint32 appInstallId, const QString &filename, quint32 crc, SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());

    uint uploadFirmwareBinary(bool recovery, const QString &filename, quint32 crc, SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());
    uint uploadFirmwareResources(const QString &filename, quint32 crc, SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());

    uint uploadFile(const QString &filename, quint32 crc, SuccessCallback successCallback = SuccessCallback(), ErrorCallback errorCallback = ErrorCallback(), ProgressCallback progressCallback = ProgressCallback());

    void cancel(uint id, int code = 0);

signals:

private:
    enum State {
        StateNotStarted,
        StateWaitForToken,
        StateInProgress,
        StateCommit,
        StateComplete
    };

    struct PendingUpload {
        uint id;

        WatchConnection::UploadType type;
        int index = -1;
        QString filename;
        quint32 appInstallId;
        QIODevice *device;
        int size;
        int remaining;
        quint32 crc;

        SuccessCallback successCallback;
        ErrorCallback errorCallback;
        ProgressCallback progressCallback;
    };

    enum PutBytesCommand {
        PutBytesCommandInit = 1,
        PutBytesCommandSend = 2,
        PutBytesCommandCommit = 3,
        PutBytesCommandAbort = 4,
        PutBytesCommandComplete = 5
    };

    void startNextUpload();
    bool uploadNextChunk(PendingUpload &upload);
    bool commit(PendingUpload &upload);
    bool complete(PendingUpload &upload);

private slots:
    void handlePutBytesMessage(const QByteArray &msg);

private:
    WatchConnection *m_connection;
    QQueue<PendingUpload> _pending;
    uint _lastUploadId;
    State _state;
    quint32 _token;
};

#endif // UPLOADMANAGER_H
