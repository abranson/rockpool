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
        RoleId
    };

    explicit NotificationSourceModel(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void insert(const QString &sourceId, const QString &name, const QString &icon, const int enabled);

signals:
    void countChanged();

private:
    NotificationSourceItem createNotificationItem(const QString &sourceId, const QString &name, const QString &icon);

private:
    QList<NotificationSourceItem> m_sources;
};

#endif // NOTIFICATIONSOURCEMODEL_H
