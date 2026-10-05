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

namespace {

const char cohortsUrl[] = "https://cohorts.rebble.io/cohort";

struct FirmwareVersion
{
    bool valid = false;
    int major = 0;
    int minor = 0;
    int patch = 0;
};

FirmwareVersion parseFirmwareVersion(const QString &version)
{
    QString numeric = version.trimmed();
    if (numeric.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        numeric.remove(0, 1);
    }
    numeric = numeric.section(QLatin1Char('-'), 0, 0);

    const QStringList fields = numeric.split(QLatin1Char('.'));
    if (fields.size() < 2 || fields.size() > 3) {
        return FirmwareVersion();
    }

    bool majorOk = false;
    bool minorOk = false;
    bool patchOk = true;
    FirmwareVersion parsed;
    parsed.major = fields.at(0).toInt(&majorOk);
    parsed.minor = fields.at(1).toInt(&minorOk);
    if (fields.size() == 3) {
        parsed.patch = fields.at(2).toInt(&patchOk);
    }
    parsed.valid = majorOk && minorOk && patchOk;
    return parsed;
}

bool isNewer(const FirmwareVersion &candidate, const FirmwareVersion &current)
{
    if (candidate.major != current.major) {
        return candidate.major > current.major;
    }
    if (candidate.minor != current.minor) {
        return candidate.minor > current.minor;
    }
    return candidate.patch > current.patch;
}

bool isSha256(const QByteArray &digest)
{
    if (digest.size() != 64) {
        return false;
    }
    foreach (const char c, digest) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

}

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

void FirmwareDownloader::clearUpdateCandidate()
{
    const bool changed = m_updateAvailable
            || !m_candidateVersion.isEmpty()
            || !m_releaseNotes.isEmpty()
            || !m_url.isEmpty()
            || !m_hash.isEmpty();
    m_updateAvailable = false;
    m_candidateVersion.clear();
    m_releaseNotes.clear();
    m_url.clear();
    m_hash.clear();
    if (changed) {
        emit updateAvailableChanged();
    }
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
    const QString platformString = m_pebble->platformString();
    if(platformString.isEmpty()) {
        qWarning() << "Hardware revision not supported for firmware upgrades" << m_pebble->hardwareRevision();
        clearUpdateCandidate();
        return;
    }

    QUrl url(QString::fromLatin1(cohortsUrl));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("select"), QStringLiteral("fw"));
    query.addQueryItem(QStringLiteral("hardware"), platformString);
    query.addQueryItem(QStringLiteral("mobilePlatform"), QStringLiteral("android"));
    query.addQueryItem(QStringLiteral("mobileVersion"), QStringLiteral("4.4.2"));
    query.addQueryItem(QStringLiteral("mobileHardware"), QStringLiteral("sailfish"));
    query.addQueryItem(QStringLiteral("pebbleAppVersion"), QStringLiteral("4.4.2"));
    url.setQuery(query);
    qDebug() << "fetching firmware info:" << url;
    QNetworkRequest request(url);
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Error fetching firmware info" << reply->errorString();
            clearUpdateCandidate();
            return;
        }

        QJsonParseError error;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            qWarning() << "Error parsing firmware fetch reply" << error.errorString();
            clearUpdateCandidate();
            return;
        }

        const QVariantMap resultMap = jsonDoc.toVariant().toMap();
        const QVariantMap targetFirmware = resultMap.value(QStringLiteral("fw"))
                .toMap().value(QStringLiteral("normal")).toMap();
        if (targetFirmware.isEmpty()) {
            qWarning() << "Could not find normal firmware package for" << m_pebble->platformString();
            clearUpdateCandidate();
            return;
        }

        const QString candidateVersion = targetFirmware.value(QStringLiteral("friendlyVersion")).toString();
        const FirmwareVersion candidate = parseFirmwareVersion(candidateVersion);
        const FirmwareVersion current = parseFirmwareVersion(m_pebble->softwareVersion());
        if (!candidate.valid || (!m_pebble->recovery() && !current.valid)) {
            qWarning() << "Could not compare firmware versions"
                       << m_pebble->softwareVersion() << candidateVersion;
            clearUpdateCandidate();
            return;
        }

        qDebug() << "current:" << m_pebble->softwareVersion()
                 << "candidate:" << candidateVersion
                 << "recovery:" << m_pebble->recovery();
        if (!m_pebble->recovery() && !isNewer(candidate, current)) {
            qDebug() << "Watch firmware is up to date";
            clearUpdateCandidate();
            return;
        }

        const QUrl firmwareUrl(targetFirmware.value(QStringLiteral("url")).toString());
        const QByteArray hash = targetFirmware.value(QStringLiteral("sha-256"))
                .toByteArray().trimmed().toLower();
        if (!firmwareUrl.isValid() || firmwareUrl.scheme() != QStringLiteral("https")
                || firmwareUrl.host().isEmpty() || !isSha256(hash)) {
            qWarning() << "Invalid firmware metadata for" << m_pebble->platformString();
            clearUpdateCandidate();
            return;
        }

        m_candidateVersion = candidateVersion;
        m_releaseNotes = targetFirmware.value(QStringLiteral("notes")).toString();
        m_url = firmwareUrl.toString();
        m_hash = hash;
        m_updateAvailable = true;
        qDebug() << "candidate firmware upgrade" << m_candidateVersion << m_url;
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
