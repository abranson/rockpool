#ifndef NOTIFICATIONSOURCEMODEL_H
#define NOTIFICATIONSOURCEMODEL_H

#include <QAbstractListModel>

class NotificationSourceItem
{
public:
    QString m_id;
    QString m_displayName;
    QString m_icon;
    int m_enabled = 0;
    // Per-app appearance overrides (empty = use the watch's resolved default).
    // m_colorName is a TimelineColor.name, m_iconCode a TimelineIcon.code.
    QString m_colorName;
    QString m_iconCode;

    bool operator ==(const NotificationSourceItem &other) {
        return m_id == other.m_id;
    }
};

class NotificationSourceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        RoleName,
        RoleEnabled,
        RoleIcon,
        RoleId,
        RoleColorName,
        RoleIconCode
    };

    explicit NotificationSourceModel(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void insert(const QString &sourceId, const QString &name, const QString &icon, const int enabled);
    // Update only the appearance override of an existing entry (no-op if unknown). Kept separate
    // from insert() because the NotificationFilterChanged signal path carries no colour/icon.
    void setAppearance(const QString &sourceId, const QString &colorName, const QString &iconCode);
    void setColorName(const QString &sourceId, const QString &colorName);
    void setIconCode(const QString &sourceId, const QString &iconCode);

signals:
    void countChanged();

private:
    QString resolveIcon(const QString &sourceId, const QString &name, const QString &icon);
    NotificationSourceItem createNotificationItem(const QString &sourceId, const QString &name, const QString &icon);

private:
    QList<NotificationSourceItem> m_sources;
    QHash<QString, QString> m_applicationIcons;
    bool m_applicationIconsLoaded = false;
};

#endif // NOTIFICATIONSOURCEMODEL_H
