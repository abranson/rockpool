#include "appdownloader.h"
#include "pebble.h"
#include "ziphelper.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

AppDownloader::AppDownloader(const QString &storagePath, QObject *parent) :
    QObject(parent),
    m_storagePath(storagePath + "/apps/")
{
    //m_nam = new QNetworkAccessManager(this);
    m_nam = ((Pebble*)parent)->nam();
}

void AppDownloader::downloadApp(const QString &id)
{
    QNetworkRequest request(QUrl("https://appstore-api.rebble.io/api/v1/apps/id/" + id));
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, &AppDownloader::appJsonFetched);
}

void AppDownloader::appJsonFetched()
{
    QNetworkReply *reply = static_cast<QNetworkReply*>(sender());
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Error fetching App Json" << reply->errorString();
        return;
    }

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Error parsing App Json" << error.errorString();
        return;
    }

    QVariantMap map = jsonDoc.toVariant().toMap();
    if (!map.contains("data") || map.value("data").toList().length() == 0) {
        qWarning() << "Unexpected json content:" << jsonDoc.toJson();
        return;
    }
    QVariantMap appMap = map.value("data").toList().first().toMap();
    QString pbwFileUrl = appMap.value("latest_release").toMap().value("pbw_file").toString();
    if (pbwFileUrl.isEmpty()) {
        qWarning() << "pbw file url empty." << jsonDoc.toJson();
        return;
    }

    QString appid = appMap.value("id").toString();
    QUuid quuid = appMap.value("uuid").toUuid();
    QDir dir;
    Pebble *p = (Pebble *)parent();
    if(p->installedAppIds().contains(quuid)) {
        AppInfo ai = p->appInfo(quuid);
        QString exId = ai.storeId();
        if(appid != exId && !dir.exists(m_storagePath+appid) && dir.exists(m_storagePath+exId)) {
            dir.rename(m_storagePath+exId,m_storagePath+appid);
        } else if(appid != exId) {
            qWarning() << "App exists but dir is out of sync:" << exId << "<!>" << appid;
        }
    } else {
        dir.mkpath(m_storagePath + appid);
    }

    QString iconFile = appMap.value("list_image").toMap().value("144x144").toString();
    QNetworkRequest request(iconFile);
    QNetworkReply *imageReply = m_nam->get(request);
    qDebug() << "fetching image" << iconFile;
    connect(imageReply, &QNetworkReply::finished, [this, imageReply, appid]() {
        imageReply->deleteLater();
        QString targetFile = m_storagePath + appid + "/list_image.png";
        qDebug() << "saving image to" << targetFile;
        QFile f(targetFile);
        if (f.open(QFile::WriteOnly)) {
            f.write(imageReply->readAll());
            f.close();
        }
    });
    appid += ("/v" + appMap.value("latest_release").toMap().value("version").toString() + ".pbw");
    fetchPackage(pbwFileUrl, appid);
}

void AppDownloader::fetchPackage(const QString &url, const QString &file)
{
    QNetworkRequest request(url);
    QNetworkReply *reply = m_nam->get(request);
    qDebug() << "Fetching app to" << file;
    reply->setProperty("file", file);
    connect(reply, &QNetworkReply::finished, this, &AppDownloader::packageFetched);
}

void AppDownloader::packageFetched()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    reply->deleteLater();

    QString file = reply->property("file").toString();

    QFile f(m_storagePath + file);
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
        qWarning() << "Error opening file for writing";
        return;
    }
    f.write(reply->readAll());
    f.flush();
    f.close();

    QString appid = file.split("/").first();

    if (!ZipHelper::unpackArchive(m_storagePath+file, m_storagePath + appid)) {
        qWarning() << "Error unpacking App zip file";
        return;
    }

    emit downloadFinished(appid);
}
