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
            m_sources[idx].m_enabled = enabled;
            emit dataChanged(index(idx), index(idx), {RoleEnabled});
        } else {
            beginRemoveRows(QModelIndex(), idx, idx);
            m_sources.removeAt(idx);
            endRemoveRows();
        }
    } else {
        beginInsertRows(QModelIndex(), m_sources.count(), m_sources.count());
        NotificationSourceItem item = createNotificationItem(sourceId, name, icon);
        item.m_enabled = enabled;
        m_sources.append(item);
        endInsertRows();
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
