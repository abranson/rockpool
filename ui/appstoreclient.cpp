#include "appstoreclient.h"
#include "applicationsmodel.h"
#include "rockpooloperation.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <libintl.h>

static QVariantMap supportedPlatforms(const QVariantMap &app)
{
    const QVariantMap platforms = app.value("compatibility").toMap();
    QVariantMap supported;
    for (auto it = platforms.constBegin(); it != platforms.constEnd(); ++it) {
        const QVariantMap platform = it.value().toMap();
        if (platform.value("supported").toBool())
            supported.insert(it.key(), platform.value("firmware").toMap().value("major", 1));
    }
    return supported;
}

static void setCompanionMetadata(AppItem *item, const QVariant &value);

namespace {
const QString corePrefix = QStringLiteral("core:");

QUrl storeOrigin(AppStoreClient::Source source)
{
    return source == AppStoreClient::CoreDevices
        ? QUrl(QStringLiteral("https://appstore-api.repebble.com"))
        : QUrl(QStringLiteral("https://appstore-api.rebble.io"));
}

AppStoreClient::Source sourceOf(const QString &storeId)
{
    return storeId.startsWith(corePrefix) ? AppStoreClient::CoreDevices : AppStoreClient::Rebble;
}

QUrl appDetailsUrl(const QString &storeId)
{
    const AppStoreClient::Source source = sourceOf(storeId);
    const QString id = source == AppStoreClient::CoreDevices ? storeId.mid(corePrefix.size()) : storeId;
    static const QRegularExpression validId(QStringLiteral("^[0-9a-fA-F]{24}$"));
    if (!validId.match(id).hasMatch())
        return QUrl();
    return storeOrigin(source).resolved(QUrl(QStringLiteral("/api/v1/apps/id/") + id));
}
}

AppStoreClient::AppStoreClient(QObject *parent):
    QObject(parent),
    m_nam(new QNetworkAccessManager(this)),
    m_model(new ApplicationsModel(this))
{
    connect(this, &AppStoreClient::sourceChanged, this, [this]() { ++m_catalogueEpoch; });
    connect(this, &AppStoreClient::hardwarePlatformChanged, this, [this]() { ++m_catalogueEpoch; });
}

ApplicationsModel *AppStoreClient::model() const
{
    return (f_model==0)? m_model : f_model;
}

void AppStoreClient::setModel(ApplicationsModel *p_model)
{
    f_model = p_model;
    emit modelChanged();
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
void AppStoreClient::setEnableCategories(const bool enable)
{
    m_enableCategories = enable;
    emit enableCategoriesChanged();
}

bool AppStoreClient::enableCategories() const
{
    return m_enableCategories;
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

    const QString section = type == TypeWatchface ? QStringLiteral("watchfaces")
        : m_source == CoreDevices ? QStringLiteral("watchapps-and-companions") : QStringLiteral("apps");
    QUrl storeUrl = storeOrigin(m_source).resolved(QUrl(QStringLiteral("/api/v1/home/") + section));
    storeUrl.setQuery(query);
    QNetworkRequest request(storeUrl);

    qDebug() << "fetching home" << storeUrl.toString();
    QNetworkReply *reply = m_nam->get(request);
    const Source source = m_source;
    const quint64 requestId = ++m_homeRequest;
    connect(reply, &QNetworkReply::finished, this, [this, reply, source, requestId]() {
        reply->deleteLater();
        if (requestId != m_homeRequest)
            return;
        QByteArray data = reply->readAll();

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
            m_model->insertGroup(slug,
                                 entry.toMap().value("name").toString(),
                                 // Only the home page links are relative, so prepend here.
                                 storeOrigin(source).resolved(entry.toMap().value("links").toMap().value("apps").toUrl()).toString());
        }

        QHash<QString, QString> categoryNames;
        foreach (const QVariant &entry, resultMap.value("categories").toList()) {
            categoryNames[entry.toMap().value("id").toString()] = entry.toMap().value("name").toString();
            if(m_enableCategories) {
                m_model->insertGroup(entry.toMap().value("id").toString(),
                                     entry.toMap().value("name").toString(),
                                     // Only the home page links are relative, so prepend here.
                                     storeOrigin(source).resolved(entry.toMap().value("links").toMap().value("apps").toUrl()).toString(),
                                     entry.toMap().value("icon").toMap().value("88x88").toString());
            }
        }

        foreach (const QVariant &entry, jsonDoc.toVariant().toMap().value("applications").toList()) {
            AppItem* item = parseAppItem(entry.toMap(), source);
            foreach (const QString &collection, collections.keys()) {
                if (collections.value(collection).contains(entry.toMap().value("id").toString())) {
                    item->setGroupId(collection);
                    item->setCollection(m_model->groupName(collection));
                    break;
                }
            }
            item->setCategory(categoryNames.value(entry.toMap().value("category_id").toString()));

            if(m_enableCategories) {
                item->setGroupId(entry.toMap().value("category_id").toString(),AppItem::GroupCategory);
            }
            //qDebug() << "have entry" << item->name() << item->groupId() << item->companion() << item->collection();

            /* Let's just disable ability to install, and give user ability to filter
            if (item->groupId().isEmpty() || item->companion()) {
                // Skip items that we couldn't match to a collection
                // Also skip apps that need a companion
                delete item;
                continue;
            }
            */
            m_model->insert(item);
        }
        setBusy(false);
    });


}

void AppStoreClient::fetchLink(const QString &link)
{
    fetchListing(storeOrigin(m_source).resolved(QUrl(link)), m_source);
}

void AppStoreClient::fetchInstalledListing(AppItem *item, const QString &store, bool openPage)
{
    if (openPage) {
        m_model->clear();
        setBusy(true);
    } else if (item) {
        item->setUpdateCandidate(QString(), QString());
        item->setChangeLog({});
        item->setCompatibility({});
        const QModelIndex index = model()->index(model()->indexOf(item));
        if (index.isValid())
            emit model()->dataChanged(index, index);
    }

    if (!item || item->isSystemApp() || item->storeSource().isEmpty() || m_hardwarePlatform.isEmpty()
            || (store != QStringLiteral("core") && store != QStringLiteral("rebble"))) {
        if (openPage)
            setBusy(false);
        return;
    }

    const Source source = store == QStringLiteral("core") ? CoreDevices : Rebble;
    const quint64 catalogueEpoch = m_catalogueEpoch;
    const QString hardware = m_hardwarePlatform;
    const QString version = item->version();
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.rockpool"), QStringLiteral("/org/rockpool/Manager"),
                                                   QStringLiteral("org.rockpool.Manager"), QStringLiteral("GetAppStoreListing"));
    message.setArguments({item->uuid(), item->storeId(), hardware, store, version});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 60000), this);
    const QPointer<AppItem> installed(item);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, installed, source, catalogueEpoch, version, openPage]() {
        QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        const QVariantMap result = QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
        const QVariantMap app = result.value("app").toMap();
        const QString id = app.value("id").toString();
        const QString candidateId = source == CoreDevices ? corePrefix + id : id;
        const QString candidateSource = AppItem::sourceForStoreId(candidateId);
        const QString candidateVersion = app.value("latest_release").toMap().value("version").toString();
        const QUuid candidateUuid(app.value("uuid").toString());
        if (installed && installed->version() == version && source == m_source && catalogueEpoch == m_catalogueEpoch
                && candidateSource == (source == CoreDevices ? QStringLiteral("core") : QStringLiteral("rebble"))
                && !candidateUuid.isNull() && candidateUuid == QUuid(installed->uuid())) {
            const bool update = result.value("updateAvailable").toBool();
            const bool switchBuild = !update && result.value("switchAvailable").toBool()
                    && installed->storeSource() != candidateSource;
            if (openPage) {
                m_model->insert(parseAppItem(app, source));
            } else if ((update || switchBuild) && !candidateVersion.isEmpty()) {
                installed->setCompatibility(supportedPlatforms(app));
                installed->setChangeLog(app.value("changelog").toList());
                setCompanionMetadata(installed, app.value("companions"));
                installed->setUpdateCandidate(candidateId, candidateVersion, switchBuild);
                const QModelIndex index = model()->index(model()->indexOf(installed));
                if (index.isValid())
                    emit model()->dataChanged(index, index);
            }
        }
        if (openPage)
            setBusy(false);
    });
}

void AppStoreClient::fetchListing(const QUrl &url, Source source)
{
    m_model->clear();
    setBusy(true);

    QUrl storeUrl(url);
    if (storeUrl.scheme() != QStringLiteral("https") || storeUrl.host() != storeOrigin(source).host()) {
        setBusy(false);
        return;
    }
    QUrlQuery query(storeUrl);
    // Navigation buttons are supplied based on actual navigation availability
    // Limit is preserved in links as long as it differs from default(20).
    query.removeQueryItem("limit");
    query.addQueryItem("limit", QString::number(m_limit));
    if (!query.hasQueryItem("hardware")) {
        query.addQueryItem("hardware", m_hardwarePlatform);
        query.addQueryItem("filter_hardware", "true");
    }
    storeUrl.setQuery(query);
    QNetworkRequest request(storeUrl);
    qDebug() << "fetching link" << request.url();

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, storeUrl, source]() {
        qDebug() << "fetch reply";
        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        QVariantMap resultMap = jsonDoc.toVariant().toMap();

        foreach (const QVariant &entry, resultMap.value("data").toList()) {
            AppItem *item = parseAppItem(entry.toMap(), source);
            qDebug() << "have entry" << item->name() << item->groupId() << item->companion() << item->category();
            // We filter apps in ModelFilter now. Since we didn't reqeuest additional apps to align for those
            // filtered out - it doesn't change end result. maybe slightly increases memory footprint.
            m_model->insert(item);
        }
        // Links should be built independently, otherwise we cannot go Previous from last page (no Next).
        int currentOffset = resultMap.value("offset").toInt();
        if (currentOffset > 0) {
            QUrl previousLink(storeUrl);
            QUrlQuery query(previousLink);
            query.removeQueryItem("offset");
            query.addQueryItem("offset", QString::number(qMax(0, currentOffset - m_limit)));
            previousLink.setQuery(query);
            m_model->addLink(previousLink.toString(), gettext("Previous"));
        }
        // NextLink is provided when there's more to fetch, no need for *haveMore* hack.
        if (resultMap.contains("links") && resultMap.value("links").toMap().contains("nextPage") &&
                !resultMap.value("links").toMap().value("nextPage").isNull()) {
            QString nextLink = resultMap.value("links").toMap().value("nextPage").toString();
            m_model->addLink(storeUrl.resolved(QUrl(nextLink)).toString(), gettext("Next"));
        }
        setBusy(false);
    });

}

void AppStoreClient::fetchAppDetails(const QString &appId)
{
    QUrl url = appDetailsUrl(appId);
    if (url.isEmpty())
        return;
    QUrlQuery query;
    if (!m_hardwarePlatform.isEmpty()) {
        query.addQueryItem("hardware", m_hardwarePlatform);
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply * reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId]() {
        reply->deleteLater();
        AppItem *item = model()->findByStoreId(appId);
        if (!item) {
            qWarning() << "Can't find item with id" << appId;
            return;
        }
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
        if(jsonDoc.isEmpty()) {
            qDebug() << "Not a store app" << appId << "or problem with store connection" << reply->errorString();
            return;
        } else
            qDebug() << "Attempting to parse for" << appId << item->category() << "cat";
        const QVariantMap replyMap = jsonDoc.toVariant().toMap().value("data").toList().value(0).toMap();
        if (replyMap.isEmpty() || (!item->uuid().isEmpty()
                && QUuid(replyMap.value("uuid").toString()) != QUuid(item->uuid())))
            return;
        item->setVersion(replyMap.value("latest_release").toMap().value("version").toString());
        if (replyMap.contains("header_images") && replyMap.value("header_images").toList().count() > 0) {
            item->setHeaderImage(replyMap.value("header_images").toList().first().toMap().value("orig").toString());
        }
        item->setVendor(replyMap.value("author").toString());
        item->setIsWatchFace(replyMap.value("type").toString() == "watchface");
        item->setCompatibility(supportedPlatforms(replyMap));
        setCompanionMetadata(item, replyMap.value("companions"));
    });
}

void AppStoreClient::search(const QString &searchString, Type type)
{
    search(searchString, type, 0);
}

void AppStoreClient::search(const QString &searchString, Type type, int page)
{
    m_model->clear();
    setBusy(true);

    const Source source = m_source;
    const bool core = source == CoreDevices;
    QUrl url(core ? QStringLiteral("https://gm3s9tryo4-dsn.algolia.net/1/indexes/apps/query")
                  : QStringLiteral("https://7683ow76eq-dsn.algolia.net/1/indexes/rebble-appstore-production/query"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("X-Algolia-Application-Id", core ? "GM3S9TRYO4" : "7683OW76EQ");
    request.setRawHeader("X-Algolia-API-Key", core ? "0b83b4f8e4e8e9793d2f1f93c21894aa"
                                                 : "252f4938082b8693a8a9fc0157d1d24f");

    const QString typeTag = type == TypeWatchface ? QStringLiteral("watchface") : QStringLiteral("watchapp");
    QJsonArray tags;
    tags.append(typeTag);
    if (core && !m_hardwarePlatform.isEmpty())
        tags.append(m_hardwarePlatform);
    QJsonObject json;
    json.insert("query", searchString);
    json.insert("hitsPerPage", 30);
    json.insert("page", page);
    json.insert("tagFilters", tags);
    if (!core) {
        json.insert("analyticsTags", QJsonArray{QStringLiteral("product-variant-time"), m_hardwarePlatform,
                                               typeTag + QStringLiteral("s"), QStringLiteral("appstore-search")});
    }
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_nam->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, source]() {
        m_model->clear();
        setBusy(false);

        reply->deleteLater();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());

        QVariantMap resultMap = jsonDoc.toVariant().toMap();
        foreach (const QVariant &entry, resultMap.value("hits").toList()) {
            AppItem *item = parseAppItem(entry.toMap(), source);
            m_model->insert(item);
//            qDebug() << "have item" << item->storeId() << item->name() << item->icon();
        }
        // Add pagination. The extra search parameters will be used as query and page. The link is just an identifier.
        if (resultMap.value("page").toInt() > 0) {
            m_model->addLink("previous", gettext("Previous"), resultMap.value("query").toString(), resultMap.value("page").toInt() - 1);
        }
        if (resultMap.value("page").toInt() < resultMap.value("nbPages").toInt() - 1) {
            m_model->addLink("next", gettext("Next"), resultMap.value("query").toString(), resultMap.value("page").toInt() + 1);
        }
        qDebug() << "Found" << m_model->rowCount() << "items";
    });
}

static void setCompanionMetadata(AppItem *item, const QVariant &value)
{
    if (value.type() == QVariant::String) {
        // Search results omit required and PebbleKit version. Fetch details before installation.
        const bool present = value.toString() != QStringLiteral("00") && !value.toString().isEmpty();
        item->setCompanionRequirement(present, present ? AppItem::UnknownCompanion : AppItem::NoCompanionRequired);
        return;
    }
    const QVariantMap companions = value.toMap();
    const QVariantMap android = companions.value(QStringLiteral("android")).toMap();
    const QVariantMap ios = companions.value(QStringLiteral("ios")).toMap();
    AppItem::CompanionRequirement requirement = AppItem::NoCompanionRequired;
    if (!android.isEmpty() && android.value(QStringLiteral("required"), true).toBool()) {
        const QString major = android.value(QStringLiteral("pebblekit_version")).toString().section('.', 0, 0);
        requirement = major == QStringLiteral("2") ? AppItem::UnsupportedAndroid : AppItem::ClassicAndroid;
    } else if (android.isEmpty() && !ios.isEmpty() && ios.value(QStringLiteral("required"), true).toBool()) {
        requirement = AppItem::IosOnly;
    }
    item->setCompanionRequirement(!android.isEmpty() || !ios.isEmpty(), requirement);
}

AppItem* AppStoreClient::parseAppItem(const QVariantMap &map, Source source)
{
    AppItem *item = new AppItem();
    const QString id = map.value("id").toString();
    item->setStoreId(source == CoreDevices ? corePrefix + id : id);
    const QUuid uuid(map.value("uuid").toString());
    if (!uuid.isNull())
        item->setUuid(uuid.toString());
    item->setName(map.value("title").toString());
    item->setVendor(map.value("author").toString());
    item->setVersion(map.value("latest_release").toMap().value("version", map.value("version")).toString());
    item->setIsWatchFace(map.value("type").toString() == "watchface");
    const QVariantList headerImages = map.value("header_images").toList();
    if (!headerImages.isEmpty())
        item->setHeaderImage(headerImages.first().toMap().value("orig").toString());
    if (!map.value("list_image").toString().isEmpty()) {
        item->setIcon(map.value("list_image").toString());
    } else {
        item->setIcon(map.value("list_image").toMap().value("144x144").toString());
    }
    item->setDescription(map.value("description").toString());
    item->setHearts(map.value("hearts").toInt());
    item->setCategory(map.value("category_name", map.value("category")).toString());
    // The search returns the companions as 00 01 10 11 string instead of a map.
    item->setCompatibility(supportedPlatforms(map));
    setCompanionMetadata(item, map.value("companions"));

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

void AppStoreClient::resetVote()
{
    ++m_voteEpoch;
    m_voteApp.clear();
    m_voteBusy = false;
    m_voteReady = false;
    m_voted = false;
    m_voteError.clear();
    emit voteChanged();
}

void AppStoreClient::fetchVote(AppItem *app)
{
    if (!m_voteBusy)
        requestVote(app, false);
}

void AppStoreClient::toggleVote(AppItem *app)
{
    if (!m_voteBusy && m_voteReady && m_voteApp == app)
        requestVote(app, true);
}

void AppStoreClient::requestVote(AppItem *app, bool change)
{
    if (!app || appDetailsUrl(app->storeId()).isEmpty())
        return;

    const quint64 epoch = ++m_voteEpoch;
    m_voteApp = app;
    m_voteBusy = true;
    m_voteReady = false;
    m_voteError.clear();
    emit voteChanged();

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("io.rebble.libpebble3"),
        QStringLiteral("/io/rebble/libpebble3/Manager"),
        QStringLiteral("io.rebble.libpebble3.Account1"),
        change ? QStringLiteral("SetAppVote") : QStringLiteral("GetAppVote"));
    message << app->storeId();
    if (change)
        message << !m_voted;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, epoch]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (epoch != m_voteEpoch)
            return;
        if (reply.isError() || !reply.value().path().startsWith(
                    QStringLiteral("/io/rebble/libpebble3/Operations/"))) {
            finishVote(epoch, QVariantMap(), false);
            return;
        }
        RockpoolOperation *operation = new RockpoolOperation(reply.value(), this);
        connect(operation, &RockpoolOperation::completed, this,
                [this, operation, epoch](bool success) {
            finishVote(epoch, operation->result(), success);
            operation->deleteLater();
        });
    });
}

void AppStoreClient::finishVote(quint64 epoch, const QVariantMap &result, bool success)
{
    if (epoch != m_voteEpoch)
        return;

    m_voteBusy = false;
    m_voteReady = success && m_voteApp && result.contains("voted")
        && result.contains("hearts") && result.value("hearts").toInt() >= 0;
    if (m_voteReady) {
        m_voted = result.value("voted").toBool();
        m_voteApp->setHearts(result.value("hearts").toInt());
    } else {
        m_voteError = tr("Could not load or update the vote. Tap to retry.");
    }
    emit voteChanged();
}
