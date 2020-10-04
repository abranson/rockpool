#include "blobdb.h"
#include "pebble.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

#include <QDebug>
#include <math.h>

BlobDB::BlobDB(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointBlobDB, this, "blobCommandReply");
    m_connection->registerEndpointHandler(WatchConnection::EndpointNotify, this, "blobUpdateNotify");

    connect(m_connection, &WatchConnection::watchConnected, [this]() {
        if (m_currentCommand) {
            delete m_currentCommand;
            m_currentCommand = nullptr;
        }
    });
}

void BlobDB::insert(BlobDBId database, const BlobDbItem &item)
{
    sendCommand(database,OperationInsert,item.itemKey(),item.serialize());
}

void BlobDB::remove(BlobDB::BlobDBId database, const QByteArray &key)
{
    sendCommand(database, OperationDelete, key);
}

void BlobDB::clear(BlobDB::BlobDBId database)
{
    sendCommand(database,OperationClear);
}

void BlobDB::setUnits(bool imperial)
{
    sendCommand(BlobDBIdAppSettings,OperationInsert,"unitsDistance",QByteArray(1,(imperial)?'\1':'\0'));
}

void BlobDB::sendCommand(BlobDBId database, Operation operation, const QByteArray &key, const QByteArray &value)
{
    if (!m_connection->isConnected()) {
        emit blobCommandResult(database,operation,key,StatusIgnore);
        return;
    }
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = operation;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    cmd->m_key = key;
    cmd->m_value = value;

    m_commandQueue.append(cmd);
    sendNext();
}

static QString BlobDBErrMsg[12]={
    "Unknown",
    "Success",
    "General Failure",
    "Invalid Operation",
    "Invalid BlobId",
    "Invalid Data",
    "No Such UUID",
    "BlobDb is Full",
    "BlobDb is Stale",
    "Operation Not Supported",
    "Database is locked",
    "Try Later"
    };

void BlobDB::blobCommandReply(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint16 token = reader.readLE<quint16>();
    Status status = (Status)reader.read<quint8>();
    if (m_currentCommand == nullptr || m_currentCommand->m_token != token) {
        qWarning() << "Received reply for unexpected token";
        return;
    } else if (status != StatusSuccess) {
        qWarning() << "Blob Command failed:" << status << BlobDBErrMsg[status];
    }
    emit blobCommandResult(m_currentCommand->m_database, m_currentCommand->m_command, m_currentCommand->m_key, status);
    delete m_currentCommand;
    m_currentCommand = nullptr;
    sendNext();
}

void BlobDB::sendNext()
{
    if (m_currentCommand || m_commandQueue.isEmpty()) {
        return;
    }
    m_currentCommand = m_commandQueue.takeFirst();
    m_connection->writeToPebble(WatchConnection::EndpointBlobDB, m_currentCommand->serialize());
}

void BlobDB::blobUpdateNotify(const QByteArray &data)
{
    BlobCommand cmd;
    if(cmd.deserialize(data)) {
        emit blobNotifyUpdate(cmd.m_database,cmd.m_command,cmd.m_timestamp,cmd.m_key,cmd.m_value);
    } else {
        qWarning() << "Could not understand" << data.toHex();
    }
}

quint16 BlobDB::generateToken()
{
    return (qrand() % ((int)pow(2, 16) - 2)) + 1;
}

QByteArray BlobDB::BlobCommand::serialize() const
{
    QByteArray ret;
    ret.append((quint8)m_command);
    ret.append(m_token & 0xFF); ret.append(((m_token >> 8) & 0xFF));
    ret.append((quint8)m_database);

    if (m_command == BlobDB::OperationInsert || m_command == BlobDB::OperationDelete) {
        ret.append(m_key.length() & 0xFF);
        ret.append(m_key);
    }
    if (m_command == BlobDB::OperationInsert) {
        ret.append(m_value.length() & 0xFF); ret.append((m_value.length() >> 8) & 0xFF); // value length
        ret.append(m_value);
    }

    return ret;
}

bool BlobDB::BlobCommand::deserialize(const QByteArray &data)
{
    WatchDataReader r(data);
    m_command = (BlobDB::Operation)r.read<quint8>();
    m_token = r.readLE<quint16>();
    m_database = (BlobDB::BlobDBId)r.read<quint8>();
    m_timestamp = r.readLE<quint32>();
    quint8 kl = r.read<quint8>();
    m_key = r.readBytes(kl);
    quint16 vl = r.readLE<quint16>();
    if(r.checkBad(vl)) return false;
    m_value = r.readBytes(vl);
    return true;
}
