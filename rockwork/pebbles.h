#ifndef PEBBLES_H
#define PEBBLES_H

#include <QObject>
#include <QAbstractListModel>
#include <QDBusServiceWatcher>
#include <QDBusObjectPath>
#include <QSet>

class Pebble;
class QDBusPendingCallWatcher;
class RockpoolAccount;

class Pebbles : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool connectedToService READ connectedToService NOTIFY connectedToServiceChanged)
    Q_PROPERTY(QString version READ version NOTIFY versionChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList scanResults READ scanResults NOTIFY scanResultsChanged)
public:
    enum Roles {
        RoleAddress,
        RoleName,
        RoleSerialNumber,
        RoleConnected,
        RoleConnectionState
    };

    Pebbles(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool connectedToService();
    QString version() const;

    Q_INVOKABLE Pebble *get(int index) const;
    int find(const QString &address) const;

    bool scanning() const;
    QVariantList scanResults() const;

    // BLE pairing goes through the daemon (org.rockwork.Manager scan API),
    // not the system Bluetooth settings.
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void connectWatch(const QString &address);
    // Stop reconnecting but stay paired. Only takes effect while the watch is connected or
    // attempting to; an idle known watch has to be forgotten instead.
    Q_INVOKABLE void disconnectWatch(const QString &address);
    // Unpair: the daemon keeps retrying a watch forever until it is told to forget it.
    Q_INVOKABLE void forgetWatch(const QString &address);

signals:
    void connectedToServiceChanged();
    void versionChanged();
    void countChanged();
    void pebbleIdentityAvailable(const QString &address);
    void scanningChanged();
    void scanResultsChanged();

private slots:
    void refresh();
    void watchListReplyFinished(QDBusPendingCallWatcher *watcher);
    void refreshVersion();
    void versionReplyFinished(QDBusPendingCallWatcher *watcher);

    void pebbleIdentityChanged();
    void pebbleConnectedChanged();
    void onScanningChanged(bool scanning);
    void refreshScanning();
    void scanningReplyFinished(QDBusPendingCallWatcher *watcher);
    void refreshScanResults();
    void scanResultsReplyFinished(QDBusPendingCallWatcher *watcher);
    void managerCommandReplyFinished(QDBusPendingCallWatcher *watcher);

private:
    int find(const QDBusObjectPath &path) const;
    void resortPebbles();
    void sendManagerCommand(const QString &method,
                            const QVariantList &arguments = QVariantList());
    static bool sortPebbles(Pebble *a, Pebble *b);

private:
    bool m_connectedToService = false;
    QString m_version;
    QList<Pebble*> m_pebbles;
    QSet<Pebble*> m_pebblesWithIdentity;
    RockpoolAccount *m_account;
    QDBusServiceWatcher *m_watcher;
    bool m_scanning = false;
    QVariantList m_scanResults;
    quint64 m_serviceEpoch = 0;
    quint64 m_watchListEpoch = 0;
    quint64 m_versionEpoch = 0;
    quint64 m_scanningEpoch = 0;
    quint64 m_scanResultsEpoch = 0;
};

#endif // PEBBLES_H
