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

void NotificationSourceModel::insert(const QString &sourceId, bool enabled)
{
    qDebug() << "changed" << sourceId << enabled;

    int idx = -1;
    for (int i = 0; i < m_sources.count(); i++) {
        if (m_sources.at(i).m_id == sourceId) {
            idx = i;
        }
    }

    if (idx >= 0) {
        m_sources[idx].m_enabled = enabled;
        emit dataChanged(index(idx), index(idx), {RoleEnabled});
    } else {
        beginInsertRows(QModelIndex(), m_sources.count(), m_sources.count());
        NotificationSourceItem item = fromDesktopFile(sourceId);
        item.m_enabled = enabled;
        m_sources.append(item);
        endInsertRows();
    }
}

NotificationSourceItem NotificationSourceModel::fromDesktopFile(const QString &sourceId)
{
    NotificationSourceItem ret;
    ret.m_id = sourceId;
    ret.m_icon = "dialog-question-symbolic";

    QString desktopFilePath;
    QStringList appsDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    foreach (const QString &appsDir, appsDirs) {
        QDir dir(appsDir);
        QFileInfoList entries = dir.entryInfoList({sourceId + "*.desktop"});
        if (entries.count() > 0) {
            desktopFilePath = entries.first().absoluteFilePath();
            break;
        }
    }

    if (desktopFilePath.isEmpty()) {
        // Lets see if this is an indicator
        QDir dir("/usr/share/upstart/xdg/autostart/");
        QString serviceName = sourceId;
        serviceName.remove("-service");
        QFileInfoList entries = dir.entryInfoList({serviceName + "*.desktop"});
        if (entries.count() > 0) {
            desktopFilePath = entries.first().absoluteFilePath();
            if (sourceId == "indicator-power-service") {
                ret.m_icon = "gpm-battery-050";
            } else if (sourceId == "indicator-datetime-service") {
                ret.m_icon = "alarm-clock";
            } else {
                ret.m_icon = "settings";
            }
        }
    }

    if (desktopFilePath.isEmpty()) {
        qWarning() << ".desktop file not found for" << sourceId;
        ret.m_displayName = sourceId;
        return ret;
    }

    QSettings s(desktopFilePath, QSettings::IniFormat);
    s.beginGroup("Desktop Entry");
    ret.m_displayName = s.value("Name").toString();
    if (!s.value("Icon").toString().isEmpty()) {
        ret.m_icon = s.value("Icon").toString();
    }

    qDebug() << "parsed file:" << desktopFilePath << ret.m_displayName << ret.m_icon;
    return ret;
}

