#include "uploadmanager.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

static const int CHUNK_SIZE = 2000;

UploadManager::UploadManager(WatchConnection *connection, QObject *parent) :
    QObject(parent), m_connection(connection),
    _lastUploadId(0), _state(StateNotStarted)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointPutBytes, this, "handlePutBytesMessage");
}

uint UploadManager::upload(WatchConnection::UploadType type, int index, quint32 appInstallId, const QString &filename, int size, quint32 crc,
                           SuccessCallback successCallback, ErrorCallback errorCallback, ProgressCallback progressCallback)
{
    qDebug() << "Should enqueue uplodad:" << filename;
    PendingUpload upload;
    upload.id = ++_lastUploadId;
    upload.type = type;
    upload.index = index;
    upload.filename = filename;
    upload.appInstallId = appInstallId;
    QFile *f = new QFile(filename);
    if (!f->open(QFile::ReadOnly)) {
        qWarning() << "Error opening file" << filename << "for reading. Cannot upload file";
        if (errorCallback) {
            errorCallback(-1);
        }
    }
    upload.device = f;
    if (size < 0) {
        upload.size = f->size();
    } else {
        upload.size = size;
    }
    upload.remaining = upload.size;
    upload.crc = crc;
    upload.successCallback = successCallback;
    upload.errorCallback = errorCallback;
    upload.progressCallback = progressCallback;

    if (upload.remaining <= 0) {
        qWarning() << "upload is empty";
        if (errorCallback) {
            errorCallback(-1);
            return -1;
        }
    }

    _pending.enqueue(upload);

    if (_pending.size() == 1) {
        startNextUpload();
    }

    return upload.id;
}

uint UploadManager::uploadAppBinary(quint32 appInstallId, const QString &filename, quint32 crc, SuccessCallback successCallback, ErrorCallback errorCallback, ProgressCallback progressCallback)
{
    return upload(WatchConnection::UploadTypeBinary, -1, appInstallId, filename, -1, crc, successCallback, errorCallback, progressCallback);
}

uint UploadManager::uploadAppResources(quint32 appInstallId, const QString &filename, quint32 crc, SuccessCallback successCallback, ErrorCallback errorCallback, ProgressCallback progressCallback)
{
    return upload(WatchConnection::UploadTypeResources, -1, appInstallId, filename, -1, crc, successCallback, errorCallback, progressCallback);
}

uint UploadManager::uploadFile(const QString &filename, quint32 crc, SuccessCallback successCallback, ErrorCallback errorCallback, ProgressCallback progressCallback)
{
    return upload(WatchConnection::UploadTypeFile, 0, 0, filename, -1, crc, successCallback, errorCallback, progressCallback);
}

uint UploadManager::uploadFirmwareBinary(bool recovery, const QString &filename, quint32 crc, SuccessCallback successCallback, ErrorCallback errorCallback, ProgressCallback progressCallback)
{
    return upload(recovery ? WatchConnection::UploadTypeRecovery: WatchConnection::UploadTypeFirmware, 0, 0, filename, -1, crc, successCallback, errorCallback, progressCallback);
}

uint UploadManager::uploadFirmwareResources(const QString &filename, quint32 crc, SuccessCallback successCallback, ErrorCallback errorCallback, ProgressCallback progressCallback)
{
    return upload(WatchConnection::UploadTypeSystemResources, 0, 0, filename, -1, crc, successCallback, errorCallback, progressCallback);
}

uint UploadManager::uploadAppWorker(quint32 appInstallId, const QString &filename, quint32 crc, UploadManager::SuccessCallback successCallback, UploadManager::ErrorCallback errorCallback, UploadManager::ProgressCallback progressCallback)
{
    return upload(WatchConnection::UploadTypeWorker, -1, appInstallId, filename, -1, crc, successCallback, errorCallback, progressCallback);
}

void UploadManager::cancel(uint id, int code)
{
    if (_pending.empty()) {
        qWarning() << "cannot cancel, empty queue";
        return;
    }

    if (id == _pending.head().id) {
        PendingUpload upload = _pending.dequeue();
        qDebug() << "aborting current upload" << id << "(code:" << code << ")";

        if (_state != StateNotStarted && _state != StateWaitForToken && _state != StateComplete) {
            QByteArray msg;
            WatchDataWriter writer(&msg);
            writer.write<quint8>(PutBytesCommandAbort);
            writer.write<quint32>(_token);

            qDebug() << "sending abort for upload" << id;

            m_connection->writeToPebble(WatchConnection::EndpointPutBytes, msg);
        }

        _state = StateNotStarted;
        _token = 0;

        if (upload.errorCallback) {
            upload.errorCallback(code);
        }
        upload.device->deleteLater();

        if (!_pending.empty()) {
            startNextUpload();
        }
    } else {
        for (int i = 1; i < _pending.size(); ++i) {
            if (_pending[i].id == id) {
                qDebug() << "cancelling upload" << id << "(code:" << code << ")";
                if (_pending[i].errorCallback) {
                    _pending[i].errorCallback(code);
                }
                _pending.at(i).device->deleteLater();
                _pending.removeAt(i);
                return;
            }
        }
        qWarning() << "cannot cancel, id" << id << "not found";
    }
}

void UploadManager::startNextUpload()
{
    Q_ASSERT(!_pending.empty());
    Q_ASSERT(_state == StateNotStarted);

    PendingUpload &upload = _pending.head();
    QByteArray msg;
    WatchDataWriter writer(&msg);
    writer.write<quint8>(PutBytesCommandInit);
    writer.write<quint32>(upload.remaining);
    if (upload.index != -1) {
        writer.write<quint8>(upload.type);
        writer.write<quint8>(upload.index);
        if (!upload.filename.isEmpty()) {
            writer.writeCString(upload.filename);
        }
    } else {
        writer.write<quint8>(upload.type|0x80);
        writer.writeLE<quint32>(upload.appInstallId);
    }

    qDebug().nospace() << "starting new upload " << upload.id
                         << ", size:" << upload.remaining
                         << ", type:" << upload.type
                         << ", slot:" << upload.index
                         << ", crc:" << upload.crc
                         << ", filename:" << upload.filename;

    qDebug() << msg.toHex();

    _state = StateWaitForToken;
    m_connection->writeToPebble(WatchConnection::EndpointPutBytes, msg);
}

bool UploadManager::uploadNextChunk(PendingUpload &upload)
{
    QByteArray chunk = upload.device->read(qMin<int>(upload.remaining, CHUNK_SIZE));

    if (upload.remaining < CHUNK_SIZE && chunk.size() < upload.remaining) {
        // Short read!
        qWarning() << "short read during upload" << upload.id;
        return false;
    }

    Q_ASSERT(!chunk.isEmpty());
    Q_ASSERT(_state = StateInProgress);

    QByteArray msg;
    WatchDataWriter writer(&msg);
    writer.write<quint8>(PutBytesCommandSend);
    writer.write<quint32>(_token);
    writer.write<quint32>(chunk.size());
    msg.append(chunk);

    qDebug() << "sending a chunk of" << chunk.size() << "bytes";

    m_connection->writeToPebble(WatchConnection::EndpointPutBytes, msg);

    upload.remaining -= chunk.size();

    qDebug() << "remaining" << upload.remaining << "/" << upload.size << "bytes";

    return true;
}

bool UploadManager::commit(PendingUpload &upload)
{
    Q_ASSERT(_state == StateCommit);
    Q_ASSERT(upload.remaining == 0);

    QByteArray msg;
    WatchDataWriter writer(&msg);
    writer.write<quint8>(PutBytesCommandCommit);
    writer.write<quint32>(_token);
    writer.write<quint32>(upload.crc);

    qDebug() << "commiting upload" << upload.id;

    m_connection->writeToPebble(WatchConnection::EndpointPutBytes, msg);

    return true;
}

bool UploadManager::complete(PendingUpload &upload)
{
    Q_ASSERT(_state == StateComplete);

    QByteArray msg;
    WatchDataWriter writer(&msg);
    writer.write<quint8>(PutBytesCommandComplete);
    writer.write<quint32>(_token);

    qDebug() << "completing upload" << upload.id;

    m_connection->writeToPebble(WatchConnection::EndpointPutBytes, msg);

    return true;
}

void UploadManager::handlePutBytesMessage(const QByteArray &data)
{
    if (_pending.empty()) {
        qWarning() << "putbytes message, but queue is empty!";
        return;
    }
    Q_ASSERT(!_pending.empty());
    PendingUpload &upload = _pending.head();

    WatchDataReader reader(data);
    int status = reader.read<quint8>();

    if (reader.bad() || status != 1) {
        qWarning() << "upload" << upload.id << "got error code=" << status;
        cancel(upload.id, status);
        return;
    }

    quint32 recv_token = reader.read<quint32>();

    if (reader.bad()) {
        qWarning() << "upload" << upload.id << ": could not read the token";
        cancel(upload.id, -1);
        return;
    }

    if (_state != StateNotStarted && _state != StateWaitForToken && _state != StateComplete) {
        if (recv_token != _token) {
            qWarning() << "upload" << upload.id << ": invalid token";
            cancel(upload.id, -1);
            return;
        }
    }

    switch (_state) {
    case StateNotStarted:
        qWarning() << "got packet when upload is not started";
        break;
    case StateWaitForToken:
        qDebug() << "token received";
        _token = recv_token;
        _state = StateInProgress;

        /* fallthrough */
    case StateInProgress:
        qDebug() << "moving to the next chunk";
        if (upload.progressCallback) {
            // Report that the previous chunk has been succesfully uploaded
            upload.progressCallback(1.0 - (qreal(upload.remaining) / upload.size));
        }
        if (upload.remaining > 0) {
            if (!uploadNextChunk(upload)) {
                cancel(upload.id, -1);
                return;
            }
        } else {
            qDebug() << "no additional chunks, commit";
            _state = StateCommit;
            if (!commit(upload)) {
                cancel(upload.id, -1);
                return;
            }
        }
        break;
    case StateCommit:
        qDebug() << "commited succesfully";
        if (upload.progressCallback) {
            // Report that all chunks have been succesfully uploaded
            upload.progressCallback(1.0);
        }
        _state = StateComplete;
        if (!complete(upload)) {
            cancel(upload.id, -1);
            return;
        }
        break;
    case StateComplete:
        qDebug() << "upload" << upload.id << "succesful, invoking callback";
        if (upload.successCallback) {
            upload.successCallback();
        }
        upload.device->deleteLater();
        _pending.dequeue();
        _token = 0;
        _state = StateNotStarted;
        if (!_pending.empty()) {
            startNextUpload();
        }
        break;
    default:
        qWarning() << "received message in wrong state";
        break;
    }
}
