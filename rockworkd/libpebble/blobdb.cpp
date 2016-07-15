#include "blobdb.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

#include <QDebug>
#include <QDir>
#include <QSettings>

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

    m_blobDBStoragePath = m_pebble->storagePath() + "/blobdb/";
    QDir dir(m_blobDBStoragePath);
    if (!dir.exists() && !dir.mkpath(m_blobDBStoragePath)) {
        qWarning() << "Error creating blobdb storage dir.";
        return;
    }
}

void BlobDB::clearApps()
{
    clear(BlobDBId::BlobDBIdApp);
    QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
    s.remove("");
}

void BlobDB::insertAppMetaData(const AppInfo &info,const bool force)
{
    if (!m_pebble->connected()) {
        qWarning() << "Pebble is not connected. Cannot install app";
        return;
    }

    QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
    if (s.value(info.uuid().toString(), false).toBool() && !force) {
        qWarning() << "App already in DB. Not syncing again";
        return;
    }

    AppMetadata metaData = appInfoToMetadata(info, m_pebble->hardwarePlatform());

    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = BlobDBIdApp;

    cmd->m_key = metaData.uuid().toRfc4122();
    cmd->m_value = metaData.serialize();

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::removeApp(const AppInfo &info)
{
    remove(BlobDBId::BlobDBIdApp, info.uuid());
    QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
    s.remove(info.uuid().toString());
}

void BlobDB::insert(BlobDBId database, const TimelineItem &item)
{
    if (!m_connection->isConnected()) {
        emit blobCommandResult(database,OperationInsert,item.itemId(),StatusIgnore);
        return;
    }
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    cmd->m_key = item.itemId().toRfc4122();
    cmd->m_value = item.serialize();

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::remove(BlobDB::BlobDBId database, const QUuid &uuid)
{
    if (!m_connection->isConnected()) {
        return;
    }
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationDelete;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    cmd->m_key = uuid.toRfc4122();

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::clear(BlobDB::BlobDBId database)
{
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationClear;
    cmd->m_token = generateToken();
    cmd->m_database = database;

    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::setHealthParams(const HealthParams &healthParams)
{
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = BlobDBIdAppSettings;

    cmd->m_key = "activityPreferences";
    cmd->m_value = healthParams.serialize();

    qDebug() << "Setting health params. Enabled:" << healthParams.enabled() << cmd->serialize().toHex();
    m_commandQueue.append(cmd);
    sendNext();
}

void BlobDB::setUnits(bool imperial)
{
    BlobCommand *cmd = new BlobCommand();
    cmd->m_command = BlobDB::OperationInsert;
    cmd->m_token = generateToken();
    cmd->m_database = BlobDBIdAppSettings;

    cmd->m_key = "unitsDistance";
    WatchDataWriter writer(&cmd->m_value);
    writer.write<quint8>(imperial ? 0x01 : 0x00);

    m_commandQueue.append(cmd);
    sendNext();
}
static QString BlobDBErrMsg[9]={"Unknown",
                         "Success",
                         "General Failure",
                         "Invalid Operation",
                         "Invalid BlobId",
                         "Invalid Data",
                         "No Such UUID",
                         "BlobDb is Full",
                         "BlobDb is Stale"
                        };

void BlobDB::blobCommandReply(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint16 token = reader.readLE<quint16>();
    Status status = (Status)reader.read<quint8>();
    if (m_currentCommand->m_token != token) {
        qWarning() << "Received reply for unexpected token";
    } else if (status != StatusSuccess) {
        qWarning() << "Blob Command failed:" << status << BlobDBErrMsg[status];
        emit blobCommandResult(m_currentCommand->m_database, m_currentCommand->m_command, QUuid::fromRfc4122(m_currentCommand->m_key), status);
    } else { // All is well
        if (m_currentCommand->m_database == BlobDBIdApp && m_currentCommand->m_command == OperationInsert) {
            QSettings s(m_blobDBStoragePath + "/appsyncstate.conf", QSettings::IniFormat);
            QUuid appUuid = QUuid::fromRfc4122(m_currentCommand->m_key);
            s.setValue(appUuid.toString(), true);
            emit appInserted(appUuid);
        } else {
            emit blobCommandResult(m_currentCommand->m_database, m_currentCommand->m_command, QUuid::fromRfc4122(m_currentCommand->m_key), status);
        }
    }

    if (m_currentCommand && token == m_currentCommand->m_token) {
        delete m_currentCommand;
        m_currentCommand = nullptr;
        sendNext();
    }
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
        if(cmd.m_command == OperationNotify) {
            if(cmd.m_database == BlobDBIdNotification || cmd.m_database == BlobDBIdReminder || cmd.m_database == BlobDBIdPin) {
                QUuid key = QUuid::fromRfc4122(cmd.m_key);
                QDateTime ts = QDateTime::fromTime_t(cmd.m_timestamp,Qt::UTC);
                TimelineItem val;
                if(val.deserialize(cmd.m_value)) {
                    qDebug() << "Notify" << key.toString() << "updated at" << ts.toString(Qt::ISODate);
                    emit notifyTimeline(ts,key,val);
                } else {
                    qWarning() << "Could not deserialize TimelineItem from" << cmd.m_value.toHex();
                }
                return;
            }
        }
        qDebug() << "BlobNotify not supported: Cmd" << cmd.m_command << "DB" << cmd.m_database << "Token" << cmd.m_token << "TS" << cmd.m_timestamp << "Key" << cmd.m_key.toHex() << "Value" << cmd.m_value.toHex();
    } else {
        qWarning() << "Could not understand" << data.toHex();
    }
}

quint16 BlobDB::generateToken()
{
    return (qrand() % ((int)pow(2, 16) - 2)) + 1;
}

AppMetadata BlobDB::appInfoToMetadata(const AppInfo &info, HardwarePlatform hardwarePlatform)
{
    QString binaryFile = info.file(AppInfo::FileTypeApplication, hardwarePlatform);
    QFile f(binaryFile);
    if (!f.open(QFile::ReadOnly)) {
        qWarning() << "Error opening app binary";
        return AppMetadata();
    }
    QByteArray data = f.read(512);
    WatchDataReader reader(data);
    qDebug() << "Header:" << reader.readFixedString(8);
    qDebug() << "struct Major version:" << reader.read<quint8>();
    qDebug() << "struct Minor version:" << reader.read<quint8>();
    quint8 sdkVersionMajor = reader.read<quint8>();
    qDebug() << "sdk Major version:" << sdkVersionMajor;
    quint8 sdkVersionMinor = reader.read<quint8>();
    qDebug() << "sdk Minor version:" << sdkVersionMinor;
    quint8 appVersionMajor = reader.read<quint8>();
    qDebug() << "app Major version:" << appVersionMajor;
    quint8 appVersionMinor = reader.read<quint8>();
    qDebug() << "app Minor version:" << appVersionMinor;
    qDebug() << "size:" << reader.readLE<quint16>();
    qDebug() << "offset:" << reader.readLE<quint32>();
    qDebug() << "crc:" << reader.readLE<quint32>();
    QString appName = reader.readFixedString(32);
    qDebug() << "App name:" << appName;
    qDebug() << "Vendor name:" << reader.readFixedString(32);
    quint32 icon = reader.readLE<quint32>();
    qDebug() << "Icon:" << icon;
    qDebug() << "Symbol table address:" << reader.readLE<quint32>();
    quint32 flags = reader.readLE<quint32>();
    qDebug() << "Flags:" << flags;
    qDebug() << "Num relocatable entries:" << reader.readLE<quint32>();

    f.close();
    qDebug() << "app data" << data.toHex();

    AppMetadata metadata;
    metadata.setUuid(info.uuid());
    metadata.setFlags(flags);
    metadata.setAppVersion(appVersionMajor, appVersionMinor);
    metadata.setSDKVersion(sdkVersionMajor, sdkVersionMinor);
    metadata.setAppFaceBgColor(0);
    metadata.setAppFaceTemplateId(0);
    metadata.setAppName(appName);
    metadata.setIcon(icon);
    return metadata;

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
