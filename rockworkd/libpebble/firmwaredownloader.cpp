#include "firmwaredownloader.h"
#include "ziphelper.h"
#include "pebble.h"
#include "watchconnection.h"
#include "uploadmanager.h"

#include <QNetworkAccessManager>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>

FirmwareDownloader::FirmwareDownloader(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_nam = pebble->nam();

    m_connection->registerEndpointHandler(WatchConnection::EndpointSystemMessage, this, "systemMessageReceived");
}

bool FirmwareDownloader::updateAvailable() const
{
    return m_updateAvailable;
}

QString FirmwareDownloader::candidateVersion() const
{
    return m_candidateVersion;
}

QString FirmwareDownloader::releaseNotes() const
{
    return m_releaseNotes;
}

QString FirmwareDownloader::url() const
{
    return m_url;
}

bool FirmwareDownloader::upgrading() const
{
    return m_upgradeInProgress;
}

void FirmwareDownloader::performUpgrade()
{
    if (!m_updateAvailable) {
        qWarning() << "No update available";
        return;
    }

    if (m_upgradeInProgress) {
        qWarning() << "Upgrade already in progress. Won't start another one";
        return;
    }

    m_upgradeInProgress = true;
    emit upgradingChanged();

    QNetworkRequest request(m_url);
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply](){
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Erorr fetching firmware" << reply->errorString();
            m_upgradeInProgress = false;
            emit upgradingChanged();
            return;
        }

        QByteArray data = reply->readAll();

        QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();

        if (hash != m_hash) {
            qWarning() << "Downloaded data hash doesn't match hash from target";
            m_upgradeInProgress = false;
            emit upgradingChanged();
            return;
        }

        QDir dir("/tmp/" + m_pebble->address().toString().replace(":", "_"));
        if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
            qWarning() << "Error saving file" << dir.absolutePath();
            m_upgradeInProgress = false;
            emit upgradingChanged();
            return;
        }
        QString path = "/tmp/" + m_pebble->address().toString().replace(":", "_");
        QFile f(path + "/" + reply->request().url().fileName());
        if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
            qWarning() << "Cannot open tmp file for writing" << f.fileName();
            m_upgradeInProgress = false;
            emit upgradingChanged();
            return;
        }
        f.write(data);
        f.close();

        if (!ZipHelper::unpackArchive(f.fileName(), path)) {
            qWarning() << "Error unpacking firmware archive";
            m_upgradeInProgress = false;
            emit upgradingChanged();
            return;
        }

        Bundle firmware(path);
        if (firmware.file(Bundle::FileTypeFirmware).isEmpty() || firmware.file(Bundle::FileTypeResources).isEmpty()) {
            qWarning() << "Firmware bundle file missing binary or resources";
            m_upgradeInProgress = false;
            emit upgradingChanged();
            return;
        }

        if(QFile::exists(path + "/layouts.json.auto")) {
            if(QFile::exists(m_pebble->storagePath() + "/layouts.json.auto"))
                QFile::remove(m_pebble->storagePath() + "/layouts.json.auto");
            QFile::rename(path+"/layouts.json.auto",m_pebble->storagePath()+"/layouts.json.auto");
            emit layoutsChanged();
        }

        qDebug() << "** Starting firmware upgrade **";
        m_bundlePath = path;
        m_connection->systemMessage(WatchConnection::SystemMessageFirmwareStart);

    });
}

void FirmwareDownloader::checkForNewFirmware()
{
    QString platformString = m_pebble->platformString();
    if(platformString.isEmpty()) {
        qWarning() << "Hardware revision not supported for firmware upgrades" << m_pebble->hardwareRevision();
        return;
    }

    QString url("https://binaries.rebble.io/fw/%1/latest.json");
    url = url.arg(platformString);
    qDebug() << "fetching firmware info:" << url;
    QNetworkRequest request(url);
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        QJsonParseError error;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll(), &error);
        qDebug() << "firmware info reply:" << jsonDoc.toJson();
        if (error.error != QJsonParseError::NoError) {
            qWarning() << "Error parsing firmware fetch reply" << jsonDoc.toJson(QJsonDocument::Indented);
            return;
        }
        QVariantMap resultMap = jsonDoc.toVariant().toMap();
        if (!resultMap.contains("normal")) {
            qWarning() << "Could not find normal firmware package" << jsonDoc.toJson(QJsonDocument::Indented);
            return;
        }

        qDebug() << "current:" << m_pebble->softwareVersion() << "candidate:" << resultMap.value("normal").toMap().value("friendlyVersion").toString();

        QVariantMap targetFirmware;
        if (resultMap.contains("3.x-migration") && m_pebble->softwareVersion() < "v3.0.0") {
            targetFirmware = resultMap.value("3.x-migration").toMap();
        } else if (m_pebble->softwareVersion() >= "v3.0.0" &&
                           resultMap.value("normal").toMap().value("friendlyVersion").toString() != m_pebble->softwareVersion()){
            targetFirmware = resultMap.value("normal").toMap();
        }

        if (targetFirmware.isEmpty()) {
            qDebug() << "Watch firmware is up to date";
            m_updateAvailable = false;
            emit updateAvailableChanged();
            return;
        }

        qDebug() << targetFirmware;

        m_candidateVersion = targetFirmware.value("friendlyVersion").toString();
        m_releaseNotes = targetFirmware.value("notes").toString();
        m_url = targetFirmware.value("url").toString();
        m_hash = targetFirmware.value("sha-256").toByteArray();
        m_updateAvailable = true;
        qDebug() << "candidate firmware upgrade" << m_candidateVersion << m_releaseNotes << m_url;
        emit updateAvailableChanged();
    });
}

void FirmwareDownloader::systemMessageReceived(const QByteArray &data)
{
    qDebug() << "system message" << data.toHex();

    if (!m_upgradeInProgress) {
        return;
    }

    Bundle firmware(m_bundlePath);

    qDebug() << "** Uploading firmware resources...";
    m_connection->uploadManager()->uploadFirmwareResources(firmware.file(Bundle::FileTypeResources), firmware.crc(Bundle::FileTypeResources), [this, firmware]() {
        qDebug() << "** Firmware resources uploaded. OK";

        qDebug() << "** Uploading firmware binary...";
        m_connection->uploadManager()->uploadFirmwareBinary(false, firmware.file(Bundle::FileTypeFirmware), firmware.crc(Bundle::FileTypeFirmware), [this]() {
            qDebug() << "** Firmware binary uploaded. OK";
            m_connection->systemMessage(WatchConnection::SystemMessageFirmwareComplete);
            m_upgradeInProgress = false;
            emit upgradingChanged();
        }, [this](int code) {
            qWarning() << "** ERROR uploading firmware binary" << code;
            m_connection->systemMessage(WatchConnection::SystemMessageFirmwareFail);
            m_upgradeInProgress = false;
            emit upgradingChanged();
        });
    },
    [this](int code) {
        qWarning() << "** ERROR uploading firmware resources" << code;
        m_connection->systemMessage(WatchConnection::SystemMessageFirmwareFail);
        m_upgradeInProgress = false;
        emit upgradingChanged();
    });
}

