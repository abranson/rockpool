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
            const QString displayIcon = resolveIcon(sourceId, name, icon);
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
    ret.m_icon = resolveIcon(sourceId, name, icon);

    return ret;
}

QString NotificationSourceModel::resolveIcon(const QString &sourceId, const QString &name, const QString &icon)
{
    if (!icon.isEmpty()) {
        return icon;
    }
    if (!m_applicationIconsLoaded) {
        m_applicationIconsLoaded = true;
        const QStringList directories = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        foreach (const QString &directory, directories) {
            const QFileInfoList entries = QDir(directory).entryInfoList(
                        QStringList() << QStringLiteral("*.desktop"), QDir::Files, QDir::Name);
            foreach (const QFileInfo &entry, entries) {
                QSettings desktop(entry.absoluteFilePath(), QSettings::IniFormat);
                desktop.setIniCodec("UTF-8");
                desktop.beginGroup(QStringLiteral("Desktop Entry"));
                const QString launcherIcon = desktop.value(QStringLiteral("Icon")).toString();
                if (launcherIcon.isEmpty() || desktop.value(QStringLiteral("Hidden"), false).toBool()) {
                    continue;
                }
                const QStringList identities = QStringList()
                        << entry.completeBaseName()
                        << desktop.value(QStringLiteral("X-apkd-packageName")).toString()
                        << desktop.value(QStringLiteral("StartupWMClass")).toString()
                        << desktop.value(QStringLiteral("Name")).toString();
                foreach (const QString &identity, identities) {
                    const QString key = identity.toLower();
                    if (!key.isEmpty() && !m_applicationIcons.contains(key)) {
                        m_applicationIcons.insert(key, launcherIcon);
                    }
                }
            }
        }
    }
    // Some system services post notifications on behalf of their application.
    static const QHash<QString, QString> serviceApplications = {
        {QStringLiteral("commhistoryd"), QStringLiteral("jolla-messages")},
        {QStringLiteral("messageserver5"), QStringLiteral("jolla-email")},
        {QStringLiteral("jolla-alarm-ui"), QStringLiteral("jolla-calendar")},
        {QStringLiteral("jolla-signon-ui"), QStringLiteral("jolla-settings")},
        {QStringLiteral("sailfish-osupdateservice"), QStringLiteral("jolla-settings")}
    };
    const QString source = sourceId.toLower();
    const QStringList candidates = QStringList() << source << serviceApplications.value(source)
                                                << QStringLiteral("harbour-") + source << name.toLower();
    foreach (const QString &candidate, candidates) {
        const QString resolved = m_applicationIcons.value(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    return QStringLiteral("icon-lock-information");
}
