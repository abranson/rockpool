#ifndef APPMANAGER_H
#define APPMANAGER_H

#include <QObject>
#include <QHash>
#include <QUuid>
#include "appinfo.h"
#include "watchconnection.h"

class Pebble;

class AppFetchResponse: public PebblePacket
{
public:
    enum Status {
        StatusStart = 0x01,
        StatusBusy = 0x02,
        StatusInvalidUUID = 0x03,
        StatusNoData = 0x04
    };
    AppFetchResponse(Status status = StatusNoData);
    void setStatus(Status status);

    QByteArray serialize() const override;

private:
    quint8 m_command = 1; // I guess there's only one command for now
    Status m_status = StatusNoData;
};

class AppManager : public QObject
{
    Q_OBJECT

public:
    enum Action {
        ActionGetAppBankStatus = 1,
        ActionRemoveApp = 2,
        ActionRefreshApp = 3,
        ActionGetAppBankUuids = 5
    };

    explicit AppManager(Pebble *pebble, WatchConnection *connection);

    QList<QUuid> appUuids() const;

    AppInfo info(const QUuid &uuid) const;

    void insertAppMetaData(const QUuid &uuid, bool force=false);
    void insertAppInfo(const AppInfo &info);

    QUuid scanApp(const QString &path);

    void wipeApp(const QUuid &uuid, bool force=false);
    void removeApp(const AppInfo &info);

    void setAppOrder(const QList<QUuid> &newList);

    void clearApps(bool force=false);

public slots:
    void rescan();

private slots:
    void blobdbAckHandler(quint8 db, quint8 cmd, const QByteArray &key, quint8 ack);
    void handleAppFetchMessage(const QByteArray &data);
    void sortingReply(const QByteArray &data);

signals:
    void appsChanged();
    void uploadRequested(const QString &file, quint32 appInstallId);
    void idMismatchDetected();
    void appInserted(const QUuid &uuid);

private:
    Pebble *m_pebble;
    WatchConnection *m_connection;
    QList<QUuid> m_appList;
    QHash<QUuid, AppInfo> m_apps;
    QString m_blobDBStoragePath;
};

#endif // APPMANAGER_H
