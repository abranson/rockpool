#ifndef PEBBLES_H
#define PEBBLES_H

#include <QObject>
#include <QAbstractListModel>
#include <QDBusServiceWatcher>
#include <QDBusObjectPath>

class Pebble;
class QDBusInterface;

class Pebbles : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool connectedToService READ connectedToService NOTIFY connectedToServiceChanged)
    Q_PROPERTY(QString version READ version)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        RoleAddress,
        RoleName,
        RoleSerialNumber,
        RoleConnected
    };

    Pebbles(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool connectedToService();
    QString version() const;

    Q_INVOKABLE Pebble *get(int index) const;
    int find(const QString &address) const;


signals:
    void connectedToServiceChanged();
    void countChanged();

private slots:
    void refresh();

    void pebbleConnectedChanged();

private:
    int find(const QDBusObjectPath &path) const;
    static bool sortPebbles(Pebble *a, Pebble *b);

private:
    bool m_connectedToService = false;
    QList<Pebble*> m_pebbles;
    QDBusServiceWatcher *m_watcher;
};

#endif // PEBBLES_H
