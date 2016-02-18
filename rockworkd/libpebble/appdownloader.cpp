#include "appdownloader.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"
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
    m_nam = new QNetworkAccessManager(this);
}

void AppDownloader::downloadApp(const QString &id)
{
    QNetworkRequest request(QUrl("https://api2.getpebble.com/v2/apps/id/" + id));
    QNetworkReply *reply = m_nam->get(request);
    reply->setProperty("storeId", id);
    connect(reply, &QNetworkReply::finished, this, &AppDownloader::appJsonFetched);
}

void AppDownloader::appJsonFetched()
{
    QNetworkReply *reply = static_cast<QNetworkReply*>(sender());
    reply->deleteLater();

    QString storeId = reply->property("storeId").toString();

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

    QDir dir;
    dir.mkpath(m_storagePath + storeId);

    QString iconFile = appMap.value("list_image").toMap().value("144x144").toString();
    QNetworkRequest request(iconFile);
    QNetworkReply *imageReply = m_nam->get(request);
    qDebug() << "fetching image" << iconFile;
    connect(imageReply, &QNetworkReply::finished, [this, imageReply, storeId]() {
        imageReply->deleteLater();
        QString targetFile = m_storagePath + storeId + "/list_image.png";
        qDebug() << "saving image to" << targetFile;
        QFile f(targetFile);
        if (f.open(QFile::WriteOnly)) {
            f.write(imageReply->readAll());
            f.close();
        }
    });

    fetchPackage(pbwFileUrl, storeId);
}

void AppDownloader::fetchPackage(const QString &url, const QString &storeId)
{
    QNetworkRequest request(url);
    QNetworkReply *reply = m_nam->get(request);
    reply->setProperty("storeId", storeId);
    connect(reply, &QNetworkReply::finished, this, &AppDownloader::packageFetched);
}

void AppDownloader::packageFetched()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    reply->deleteLater();

    QString storeId = reply->property("storeId").toString();

    QFile f(m_storagePath + storeId + "/" + reply->request().url().fileName() + ".zip");
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
        qWarning() << "Error opening file for writing";
        return;
    }
    f.write(reply->readAll());
    f.flush();
    f.close();

    QString zipName = m_storagePath + storeId + "/" + reply->request().url().fileName() + ".zip";

    if (!ZipHelper::unpackArchive(zipName, m_storagePath + storeId)) {
        qWarning() << "Error unpacking App zip file";
        return;
    }

    emit downloadFinished(storeId);
}
