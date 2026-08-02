#include "notificationsourcemodel.h"

#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QDebug>

NotificationSourceModel::NotificationSourceModel(QObject *parent) : QAbstractListModel(parent)
{
}

int NotificationSourceModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_sources.count();
}

QVariant NotificationSourceModel::data(const QModelIndex &index, int role) const
{
    NotificationSourceItem item = m_sources.at(index.row());
    switch (role) {
    case RoleName:
        return item.m_displayName;
    case RoleEnabled:
        return item.m_enabled;
    case RoleId:
        return item.m_id;
    case RoleIcon:
        return item.m_icon;
    case RoleColorName:
        return item.m_colorName;
    case RoleIconCode:
        return item.m_iconCode;
    }
    return QVariant();
}

QHash<int, QByteArray> NotificationSourceModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(RoleName, "name");
    roles.insert(RoleEnabled, "enabled");
    roles.insert(RoleIcon, "icon");
    roles.insert(RoleId, "id");
    roles.insert(RoleColorName, "colorName");
    roles.insert(RoleIconCode, "iconCode");
    return roles;
}

void NotificationSourceModel::insert(const QString &sourceId, const QString &name, const QString &icon, const int enabled)
{
    qDebug() << "Notification filter " << sourceId << name << icon << enabled;

    int idx = -1;
    for (int i = 0; i < m_sources.count(); i++) {
        if (m_sources.at(i).m_id == sourceId) {
            idx = i;
        }
    }

    if (idx >= 0) {
        if (enabled >= 0) {
            QVector<int> roles;
            const QString displayName = name.isEmpty() ? sourceId : name;
            const QString displayIcon = icon.isEmpty() ? "icon-lock-information" : icon;
            if (m_sources[idx].m_displayName != displayName) {
                m_sources[idx].m_displayName = displayName;
                roles.append(RoleName);
            }
            if (m_sources[idx].m_icon != displayIcon) {
                m_sources[idx].m_icon = displayIcon;
                roles.append(RoleIcon);
            }
            if (m_sources[idx].m_enabled != enabled) {
                m_sources[idx].m_enabled = enabled;
                roles.append(RoleEnabled);
            }
            if (!roles.isEmpty()) {
                emit dataChanged(index(idx), index(idx), roles);
            }
        } else {
            beginRemoveRows(QModelIndex(), idx, idx);
            m_sources.removeAt(idx);
            endRemoveRows();
            emit countChanged();
        }
    } else {
        if (enabled < 0) {
            return;
        }
        beginInsertRows(QModelIndex(), m_sources.count(), m_sources.count());
        NotificationSourceItem item = createNotificationItem(sourceId, name, icon);
        item.m_enabled = enabled;
        m_sources.append(item);
        endInsertRows();
        emit countChanged();
    }
}

void NotificationSourceModel::setAppearance(const QString &sourceId, const QString &colorName, const QString &iconCode)
{
    for (int i = 0; i < m_sources.count(); i++) {
        if (m_sources.at(i).m_id == sourceId) {
            QVector<int> roles;
            if (m_sources[i].m_colorName != colorName) {
                m_sources[i].m_colorName = colorName;
                roles.append(RoleColorName);
            }
            if (m_sources[i].m_iconCode != iconCode) {
                m_sources[i].m_iconCode = iconCode;
                roles.append(RoleIconCode);
            }
            if (!roles.isEmpty()) {
                emit dataChanged(index(i), index(i), roles);
            }
            return;
        }
    }
}

void NotificationSourceModel::setColorName(const QString &sourceId, const QString &colorName)
{
    for (int i = 0; i < m_sources.count(); i++) {
        if (m_sources.at(i).m_id == sourceId && m_sources.at(i).m_colorName != colorName) {
            m_sources[i].m_colorName = colorName;
            emit dataChanged(index(i), index(i), {RoleColorName});
            return;
        }
    }
}

void NotificationSourceModel::setIconCode(const QString &sourceId, const QString &iconCode)
{
    for (int i = 0; i < m_sources.count(); i++) {
        if (m_sources.at(i).m_id == sourceId && m_sources.at(i).m_iconCode != iconCode) {
            m_sources[i].m_iconCode = iconCode;
            emit dataChanged(index(i), index(i), {RoleIconCode});
            return;
        }
    }
}

NotificationSourceItem NotificationSourceModel::createNotificationItem(const QString &sourceId, const QString &name, const QString &icon)
{
    NotificationSourceItem ret;
    ret.m_id = sourceId;
    ret.m_displayName = name.isEmpty()?sourceId:name;
    ret.m_icon = icon.isEmpty()?"icon-lock-information":icon;

    return ret;
}
