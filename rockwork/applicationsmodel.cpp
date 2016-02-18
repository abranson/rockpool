#include "applicationsmodel.h"

#include <QDebug>


ApplicationsModel::ApplicationsModel(QObject *parent):
    QAbstractListModel(parent)
{
}

int ApplicationsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_apps.count();
}

QVariant ApplicationsModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case RoleStoreId:
        return m_apps.at(index.row())->storeId();
    case RoleUuid:
        return m_apps.at(index.row())->uuid();
    case RoleName:
        return m_apps.at(index.row())->name();
    case RoleIcon:
        return m_apps.at(index.row())->icon();
    case RoleVendor:
        return m_apps.at(index.row())->vendor();
    case RoleVersion:
        return m_apps.at(index.row())->version();
    case RoleIsWatchFace:
        return m_apps.at(index.row())->isWatchFace();
    case RoleIsSystemApp:
        return m_apps.at(index.row())->isSystemApp();
    case RoleHasSettings:
        return m_apps.at(index.row())->hasSettings();
    case RoleDescription:
        return m_apps.at(index.row())->description();
    case RoleHearts:
        return m_apps.at(index.row())->hearts();
    case RoleCategory:
        return m_apps.at(index.row())->category();
    case RoleGroupId:
        return m_apps.at(index.row())->groupId();
    }

    return QVariant();
}

QHash<int, QByteArray> ApplicationsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(RoleStoreId, "storeId");
    roles.insert(RoleUuid, "uuid");
    roles.insert(RoleName, "name");
    roles.insert(RoleIcon, "icon");
    roles.insert(RoleVendor, "vendor");
    roles.insert(RoleVersion, "version");
    roles.insert(RoleIsWatchFace, "isWatchFace");
    roles.insert(RoleIsSystemApp, "isSystemApp");
    roles.insert(RoleHasSettings, "hasSettings");
    roles.insert(RoleDescription, "description");
    roles.insert(RoleHearts, "hearts");
    roles.insert(RoleCategory, "category");
    roles.insert(RoleGroupId, "groupId");
    return roles;
}

void ApplicationsModel::clear()
{
    beginResetModel();
    qDeleteAll(m_apps);
    m_apps.clear();
    endResetModel();
    m_groupNames.clear();
    m_groupLinks.clear();
    m_links.clear();
    m_linkNames.clear();
    emit linksChanged();
    emit changed();
}

void ApplicationsModel::insert(AppItem *item)
{
    item->setParent(this);
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_apps.append(item);
    endInsertRows();
    emit changed();
}

void ApplicationsModel::insertGroup(const QString &id, const QString &name, const QString &link)
{
    m_groupNames[id] = name;
    m_groupLinks[id] = link;
}

AppItem *ApplicationsModel::get(int index) const
{
    if (index >= 0 && index < m_apps.count()) {
        return m_apps.at(index);
    }
    return nullptr;
}

AppItem *ApplicationsModel::findByStoreId(const QString &storeId) const
{
    foreach (AppItem *item, m_apps) {
        if (item->storeId() == storeId) {
            return item;
        }
    }
    return nullptr;
}

AppItem *ApplicationsModel::findByUuid(const QString &uuid) const
{
    foreach (AppItem *item, m_apps) {
        if (item->uuid() == uuid) {
            return item;
        }
    }
    return nullptr;
}

bool ApplicationsModel::contains(const QString &storeId) const
{
    foreach (AppItem* item, m_apps) {
        if (item->storeId() == storeId) {
            return true;
        }
    }
    return false;
}

int ApplicationsModel::indexOf(AppItem *item) const
{
    return m_apps.indexOf(item);
}

QString ApplicationsModel::groupName(const QString &groupId) const
{
    return m_groupNames.value(groupId);
}

QString ApplicationsModel::groupLink(const QString &groupId) const
{
    return m_groupLinks.value(groupId);
}

QString ApplicationsModel::linkName(const QString &link) const
{
    return m_linkNames.value(link);
}

QStringList ApplicationsModel::links() const
{
    return m_links;
}

void ApplicationsModel::addLink(const QString &link, const QString &name)
{
    m_links.append(link);
    m_linkNames[link] = name;
    emit linksChanged();
}

void ApplicationsModel::move(int from, int to)
{
    if (from < 0 || to < 0) {
        return;
    }
    if (from >= m_apps.count() || to >= m_apps.count()) {
        return;
    }
    if (from == to) {
        return;
    }
    int newModelIndex = to > from ? to + 1 : to;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), newModelIndex);

    m_apps.move(from, to);
    QStringList appList;
    foreach (const AppItem *item, m_apps) {
        appList << item->name();
    }
    endMoveRows();
}

void ApplicationsModel::commitMove()
{
    emit appsSorted();
}

AppItem::AppItem(QObject *parent):
    QObject(parent)
{

}

QString AppItem::storeId() const
{
    return m_storeId;
}

QString AppItem::uuid() const
{
    return m_uuid;
}

QString AppItem::name() const
{
    return m_name;
}

QString AppItem::icon() const
{
    return m_icon;
}

QString AppItem::vendor() const
{
    return m_vendor;
}

QString AppItem::version() const
{
    return m_version;
}

QString AppItem::description() const
{
    return m_description;
}

int AppItem::hearts() const
{
    return m_hearts;
}

QStringList AppItem::screenshotImages() const
{
    return m_screenshotImages;
}

bool AppItem::isWatchFace() const
{
    return m_isWatchFace;
}

bool AppItem::isSystemApp() const
{
    return m_isSystemApp;
}

bool AppItem::hasSettings() const
{
    return m_hasSettings;
}

bool AppItem::companion() const
{
    return m_companion;
}

QString AppItem::category() const
{
    return m_category;
}

QString AppItem::groupId() const
{
    return m_groupId;
}

void AppItem::setStoreId(const QString &storeId)
{
    m_storeId = storeId;
}

void AppItem::setUuid(const QString &uuid)
{
    m_uuid = uuid;
}

void AppItem::setName(const QString &name)
{
    m_name = name;
}

void AppItem::setIcon(const QString &icon)
{
    m_icon = icon;
}

void AppItem::setVendor(const QString &vendor)
{
    m_vendor = vendor;
    emit vendorChanged();
}

void AppItem::setVersion(const QString &version)
{
    m_version = version;
    emit versionChanged();
}

void AppItem::setDescription(const QString &description)
{
    m_description = description;
}

void AppItem::setHearts(int hearts)
{
    m_hearts = hearts;
}

void AppItem::setIsWatchFace(bool isWatchFace)
{
    m_isWatchFace = isWatchFace;
    emit isWatchFaceChanged();
}

void AppItem::setIsSystemApp(bool isSystemApp)
{
    m_isSystemApp = isSystemApp;
}

void AppItem::setHasSettings(bool hasSettings)
{
    m_hasSettings = hasSettings;
}

void AppItem::setCompanion(bool companion)
{
    m_companion = companion;
}

void AppItem::setCategory(const QString &category)
{
    m_category = category;
}

void AppItem::setScreenshotImages(const QStringList &screenshotImages)
{
    m_screenshotImages = screenshotImages;
}

void AppItem::setHeaderImage(const QString &headerImage)
{
    m_headerImage = headerImage;
    emit headerImageChanged();
}

void AppItem::setGroupId(const QString &groupId)
{
    m_groupId = groupId;
}

QString AppItem::headerImage() const
{
    return m_headerImage;
}

