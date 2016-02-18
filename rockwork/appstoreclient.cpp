#include "appstoreclient.h"
#include "applicationsmodel.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonParseError>

#include <libintl.h>

/* Known params for pebble api
    query.addQueryItem("offset", QString::number(offset));
    query.addQueryItem("limit", QString::number(limit));
    query.addQueryItem("image_ratio", "1"); // Not sure yet what this does
    query.addQueryItem("filter_hardware", "true");
    query.addQueryItem("firmware_version", "3");
    query.addQueryItem("hardware", hardwarePlatform);
    query.addQueryItem("platform", "all");
*/

AppStoreClient::AppStoreClient(QObject *parent):
    QObject(parent),
    m_nam(new QNetworkAccessManager(this)),
    m_model(new ApplicationsModel(this))
{
}

ApplicationsModel *AppStoreClient::model() const
{
    return m_model;
}

int AppStoreClient::limit() const
{
    return m_limit;
}

void AppStoreClient::setLimit(int limit)
{
    m_limit = limit;
    emit limitChanged();
}

QString AppStoreClient::hardwarePlatform() const
{
    return m_hardwarePlatform;
}

void AppStoreClient::setHardwarePlatform(const QString &hardwarePlatform)
{
    m_hardwarePlatform = hardwarePlatform;
    emit hardwarePlatformChanged();
}

bool AppStoreClient::busy() const
{
    return m_busy;
}

void AppStoreClient::fetchHome(Type type)
{
    m_model->clear();
    setBusy(true);

    QUrlQuery query;
    query.addQueryItem("firmware_version", "3");
    if (!m_hardwarePlatform.isEmpty()) {
        query.addQueryItem("hardware", m_hardwarePlatform);
        query.addQueryItem("filter_hardware", "true");
    }

    QString url;
    if (type == TypeWatchapp) {
        url = "https://api2.getpebble.com/v2/home/apps";
    } else {
        url = "https://api2.getpebble.com/v2/home/watchfaces";
    }
    QUrl storeUrl(url);
    storeUrl.setQuery(query);
    QNetworkRequest request(storeUrl);

    qDebug() << "fetching home" << storeUrl.toString();
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        QVariantMap resultMap = jsonDoc.toVariant().toMap();

        QHash<QString, QStringList> collections;
        foreach (const QVariant &entry, resultMap.value("collections").toList()) {
            QStringList appIds;
            foreach (const QVariant &appId, entry.toMap().value("application_ids").toList()) {
                appIds << appId.toString();
            }
            QString slug = entry.toMap().value("slug").toString();
            collections[slug] = appIds;
            m_model->insertGroup(slug, entry.toMap().value("name").toString(), entry.toMap().value("links").toMap().value("apps").toString());
        }

        QHash<QString, QString> categoryNames;
        foreach (const QVariant &entry, resultMap.value("categories").toList()) {
            categoryNames[entry.toMap().value("id").toString()] = entry.toMap().value("name").toString();
        }

        foreach (const QVariant &entry, jsonDoc.toVariant().toMap().value("applications").toList()) {
            AppItem* item = parseAppItem(entry.toMap());
            foreach (const QString &collection, collections.keys()) {
                if (collections.value(collection).contains(item->storeId())) {
                    item->setGroupId(collection);
                    break;
                }
            }
            item->setCategory(categoryNames.value(entry.toMap().value("category_id").toString()));

            qDebug() << "have entry" << item->name() << item->groupId() << item->companion();

            if (item->groupId().isEmpty() || item->companion()) {
                // Skip items that we couldn't match to a collection
                // Also skip apps that need a companion
                delete item;
                continue;
            }
            m_model->insert(item);
        }
        setBusy(false);
    });


}

void AppStoreClient::fetchLink(const QString &link)
{
    m_model->clear();
    setBusy(true);

    QUrl storeUrl(link);
    QUrlQuery query(storeUrl);
    query.removeQueryItem("limit");
    // We fetch one more than we actually want so we can see if we need to display
    // a next button
    query.addQueryItem("limit", QString::number(m_limit + 1));
    int currentOffset = query.queryItemValue("offset").toInt();
    query.removeQueryItem("offset");
    query.addQueryItem("offset", QString::number(qMax(0, currentOffset - 1)));
    if (!query.hasQueryItem("hardware")) {
        query.addQueryItem("hardware", m_hardwarePlatform);
        query.addQueryItem("filter_hardware", "true");
    }
    storeUrl.setQuery(query);
    QNetworkRequest request(storeUrl);
    qDebug() << "fetching link" << request.url();

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        qDebug() << "fetch reply";
        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        QVariantMap resultMap = jsonDoc.toVariant().toMap();

        bool haveMore = false;
        foreach (const QVariant &entry, resultMap.value("data").toList()) {
            if (model()->rowCount() >= m_limit) {
                haveMore = true;
                break;
            }
            AppItem *item = parseAppItem(entry.toMap());
            if (item->companion()) {
                // For now just skip items with companions
                delete item;
            } else {
                m_model->insert(item);
            }
        }

        if (resultMap.contains("links") && resultMap.value("links").toMap().contains("nextPage") &&
                !resultMap.value("links").toMap().value("nextPage").isNull()) {
            int currentOffset = resultMap.value("offset").toInt();
            QString nextLink = resultMap.value("links").toMap().value("nextPage").toString();

            if (currentOffset > 0) {
                QUrl previousLink(nextLink);
                QUrlQuery query(previousLink);
                query.removeQueryItem("limit");
                query.addQueryItem("limit", QString::number(m_limit + 1));
                query.removeQueryItem("offset");
                query.addQueryItem("offset", QString::number(qMax(0, currentOffset - m_limit + 1)));
                previousLink.setQuery(query);
                m_model->addLink(previousLink.toString(), gettext("Previous"));
            }
            if (haveMore) {
                m_model->addLink(nextLink, gettext("Next"));
            }
        }
        setBusy(false);
    });

}

void AppStoreClient::fetchAppDetails(const QString &appId)
{
    QUrl url("https://api2.getpebble.com/v2/apps/id/" + appId);
    QUrlQuery query;
    if (!m_hardwarePlatform.isEmpty()) {
        query.addQueryItem("hardware", m_hardwarePlatform);
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply * reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply, appId]() {
        reply->deleteLater();
        AppItem *item = m_model->findByStoreId(appId);
        if (!item) {
            qWarning() << "Can't find item with id" << appId;
            return;
        }
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
        QVariantMap replyMap = jsonDoc.toVariant().toMap().value("data").toList().first().toMap();
        if (replyMap.contains("header_images") && replyMap.value("header_images").toList().count() > 0) {
            item->setHeaderImage(replyMap.value("header_images").toList().first().toMap().value("orig").toString());
        }
        item->setVendor(replyMap.value("author").toString());
        item->setVersion(replyMap.value("latest_release").toMap().value("version").toString());
        item->setIsWatchFace(replyMap.value("type").toString() == "watchface");
    });
}

void AppStoreClient::search(const QString &searchString, Type type)
{
    m_model->clear();
    setBusy(true);

    QUrl url("https://bujatnzd81-dsn.algolia.io/1/indexes/pebble-appstore-production");
    QUrlQuery query;
    query.addQueryItem("x-algolia-api-key", "8dbb11cdde0f4f9d7bf787e83ac955ed");
    query.addQueryItem("x-algolia-application-id", "BUJATNZD81");
    query.addQueryItem("query", searchString);
    QStringList filters;
    if (type == TypeWatchapp) {
        filters.append("watchapp");
    } else if (type == TypeWatchface) {
        filters.append("watchface");
    }
    filters.append(m_hardwarePlatform);
    query.addQueryItem("tagFilters", filters.join(","));
    url.setQuery(query);

    QNetworkRequest request(url);
    qDebug() << "Search query:" << url;
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        m_model->clear();
        setBusy(false);

        reply->deleteLater();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());

        QVariantMap resultMap = jsonDoc.toVariant().toMap();
        foreach (const QVariant &entry, resultMap.value("hits").toList()) {
            AppItem *item = parseAppItem(entry.toMap());
            m_model->insert(item);
//            qDebug() << "have item" << item->name() << item->icon();
        }
        qDebug() << "Found" << m_model->rowCount() << "items";
    });
}

AppItem* AppStoreClient::parseAppItem(const QVariantMap &map)
{
    AppItem *item = new AppItem();
    item->setStoreId(map.value("id").toString());
    item->setName(map.value("title").toString());
    if (!map.value("list_image").toString().isEmpty()) {
        item->setIcon(map.value("list_image").toString());
    } else {
        item->setIcon(map.value("list_image").toMap().value("144x144").toString());
    }
    item->setDescription(map.value("description").toString());
    item->setHearts(map.value("hearts").toInt());
    item->setCategory(map.value("category_name").toString());
    item->setCompanion(!map.value("companions").toMap().value("android").isNull() || !map.value("companions").toMap().value("ios").isNull());

    QVariantList screenshotsList = map.value("screenshot_images").toList();
    // try to get more hardware specific screenshots. The store search keeps them in a subgroup.
    if (map.contains("asset_collections")) {
        foreach (const QVariant &assetCollection, map.value("asset_collections").toList()) {
            if (assetCollection.toMap().value("hardware_platform").toString() == m_hardwarePlatform) {
                screenshotsList = assetCollection.toMap().value("screenshots").toList();
                break;
            }
        }
    }
    QStringList screenshotImages;
    foreach (const QVariant &screenshotItem, screenshotsList) {
        if (!screenshotItem.toString().isEmpty()) {
            screenshotImages << screenshotItem.toString();
        } else if (screenshotItem.toMap().count() > 0) {
            screenshotImages << screenshotItem.toMap().first().toString();
        }
    }
    item->setScreenshotImages(screenshotImages);
//    qDebug() << "setting screenshot images" << item->screenshotImages();

    // The search seems to return references to invalid icon images. if we detect that, we'll replace it with a screenshot
    if (item->icon().contains("aOUhkV1R1uCqCVkKY5Dv") && !item->screenshotImages().isEmpty()) {
        item->setIcon(item->screenshotImages().first());
    }

    return item;
}

void AppStoreClient::setBusy(bool busy)
{
    m_busy = busy;
    emit busyChanged();
}

