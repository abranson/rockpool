#include <QDir>
#include <QSettings>

#include "appmanager.h"
#include "pebble.h"

#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"
#include "uploadmanager.h"

#include <libintl.h>

#define SETTINGS_APP_UUID "07e0d9cb-8957-4bf7-9d42-35bf47caadfe"

AppManager::AppManager(Pebble *pebble, WatchConnection *connection)
    : QObject(pebble),
      m_pebble(pebble),
      m_connection(connection)
{
    QDir dataDir(m_pebble->storagePath() + "/apps/");
    if (!dataDir.exists() && !dataDir.mkpath(dataDir.absolutePath())) {
        qWarning() << "could not create apps dir" << dataDir.absolutePath();
    }
    qDebug() << "install apps in" << dataDir.absolutePath();

    m_connection->registerEndpointHandler(WatchConnection::EndpointAppFetch, this, "handleAppFetchMessage");
    m_connection->registerEndpointHandler(WatchConnection::EndpointSorting, this, "sortingReply");
}

QList<QUuid> AppManager::appUuids() const
{
    return m_appList;
}

//QList<QString> AppManager::appIds() const
//{
//    return m_appsIds.keys();
//}

AppInfo AppManager::info(const QUuid &uuid) const
{
    return m_apps.value(uuid);
}

//AppInfo AppManager::info(const QString &id) const
//{
//    return m_appsUuids.value(m_appsIds.value(id));
//}

void AppManager::rescan()
{
    m_appList.clear();
    m_apps.clear();

    AppInfo settingsApp(QUuid(SETTINGS_APP_UUID), false, gettext("Settings"), gettext("System app"));
    m_appList.append(settingsApp.uuid());
    m_apps.insert(settingsApp.uuid(), settingsApp);
    AppInfo watchfaces(QUuid("18e443ce-38fd-47c8-84d5-6d0c775fbe55"), false, gettext("Watchfaces"), gettext("System app"));
    m_appList.append(watchfaces.uuid());
    m_apps.insert(watchfaces.uuid(), watchfaces);
    if (m_pebble->capabilities().testFlag(CapabilityHealth)) {
        AppInfo health(QUuid("36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c"), false, gettext("Health"), gettext("System app"), true);
        m_appList.append(health.uuid());
        m_apps.insert(health.uuid(), health);
    }
    AppInfo music(QUuid("1f03293d-47af-4f28-b960-f2b02a6dd757"), false, gettext("Music"), gettext("System app"));
    m_appList.append(music.uuid());
    m_apps.insert(music.uuid(), music);
    AppInfo notifications(QUuid("b2cae818-10f8-46df-ad2b-98ad2254a3c1"), false, gettext("Notifications"), gettext("System app"));
    m_appList.append(notifications.uuid());
    m_apps.insert(notifications.uuid(), notifications);
    AppInfo alarms(QUuid("67a32d95-ef69-46d4-a0b9-854cc62f97f9"), false, gettext("Alarms"), gettext("System app"));
    m_appList.append(alarms.uuid());
    m_apps.insert(alarms.uuid(), alarms);
    AppInfo ticToc(QUuid("8f3c8686-31a1-4f5f-91f5-01600c9bdc59"), true, "Tic Toc", gettext("Default watchface"));
    m_appList.append(ticToc.uuid());
    m_apps.insert(ticToc.uuid(), ticToc);

    QDir dir(m_pebble->storagePath() + "/apps/");
    qDebug() << "Scanning Apps dir" << dir.absolutePath();
    Q_FOREACH(const QString &path, dir.entryList(QDir::Dirs | QDir::Readable)) {
        QString appPath = dir.absoluteFilePath(path);
        if (dir.exists(path + "/appinfo.json")) {
            scanApp(appPath);
        } else if (QFileInfo(appPath).isFile()) {
            scanApp(appPath);
        }
    }

    QSettings settings(m_pebble->storagePath() + "/apps.conf", QSettings::IniFormat);
    QStringList storedList = settings.value("appList").toStringList();
    if (storedList.isEmpty()) {
        // User did not manually sort the app list yet... We can stop here.
        return;
    }
    // Run some sanity checks
    if (storedList.count() != m_appList.count()) {
        qWarning() << "Installed apps not matching order config. App sort order might be wrong.";
        return;
    }
    foreach (const QUuid &uuid, m_appList) {
        if (!storedList.contains(uuid.toString())) {
            qWarning() << "Installed apps and stored config order cannot be matched. App sort order might be wrong.";
            return;
        }
    }
    // All seems fine, repopulate m_appList
    m_appList.clear();
    foreach (const QString &storedId, storedList) {
        m_appList.append(QUuid(storedId));
    }
}

void AppManager::handleAppFetchMessage(const QByteArray &data)
{
    WatchDataReader reader(data);
    reader.read<quint8>();
    QUuid uuid = reader.readUuid();
    quint32 appFetchId = reader.read<quint32>();

    bool haveApp = m_apps.contains(uuid);

    AppFetchResponse response;
    if (haveApp) {
        response.setStatus(AppFetchResponse::StatusStart);
        m_connection->writeToPebble(WatchConnection::EndpointAppFetch, response.serialize());
    } else {
        qWarning() << "App with uuid" << uuid.toString() << "which is not installed.";
        response.setStatus(AppFetchResponse::StatusInvalidUUID);
        m_connection->writeToPebble(WatchConnection::EndpointAppFetch, response.serialize());
        emit idMismatchDetected();
        return;
    }

    AppInfo appInfo = m_apps.value(uuid);

    QString binaryFile = appInfo.file(AppInfo::FileTypeApplication, m_pebble->hardwarePlatform());
    quint32 crc = appInfo.crc(AppInfo::FileTypeApplication, m_pebble->hardwarePlatform());
    qDebug() << "opened binary" << binaryFile << "for hardware" << m_pebble->hardwarePlatform() << "crc" << crc;
    m_connection->uploadManager()->uploadAppBinary(appFetchId, binaryFile, crc, [this, appInfo, appFetchId](){
        qDebug() << "binary file uploaded successfully";

        QString resourcesFile = appInfo.file(AppInfo::FileTypeResources, m_pebble->hardwarePlatform());
        quint32 crc = appInfo.crc(AppInfo::FileTypeResources, m_pebble->hardwarePlatform());
        qDebug() << "uploadign resource file" << resourcesFile;
        m_connection->uploadManager()->uploadAppResources(appFetchId, resourcesFile, crc, [this, appInfo, appFetchId]() {
            qDebug() << "resource file uploaded successfully";

            QString workerFile = appInfo.file(AppInfo::FileTypeWorker, m_pebble->hardwarePlatform());
            if (!workerFile.isEmpty()) {
                quint32 crc = appInfo.crc(AppInfo::FileTypeWorker, m_pebble->hardwarePlatform());
                m_connection->uploadManager()->uploadAppWorker(appFetchId, workerFile, crc, [this]() {
                    qDebug() << "worker file uploaded successfully";
                });
            }
        });
    });
}

void AppManager::sortingReply(const QByteArray &data)
{
    qDebug() << "have sorting reply" << data.toHex();
}

void AppManager::insertAppInfo(const AppInfo &info)
{
    m_appList.append(info.uuid());
    m_apps.insert(info.uuid(), info);
//    m_appsIds.insert(info.id(), info.uuid());
    emit appsChanged();
}

QUuid AppManager::scanApp(const QString &path)
{
    qDebug() << "scanning app" << path;
    AppInfo info(path);
    if (info.isValid()) {
        insertAppInfo(info);
    }
    return info.uuid();
}

void AppManager::removeApp(const QUuid &uuid)
{
    m_appList.removeAll(uuid);
    AppInfo info = m_apps.take(uuid);
    if (!info.isValid() || info.path().isEmpty()) {
        qWarning() << "App UUID not found. not removing";
        return;

    }
    QDir dir(info.path());
    dir.removeRecursively();
    emit appsChanged();
}

void AppManager::setAppOrder(const QList<QUuid> &newList)
{
    // run some sanity checks
    if (newList.count() != m_appList.count()) {
        qWarning() << "Number of apps in order list is not matching installed apps.";
        return;
    }
    foreach (const QUuid &installedUuid, m_appList) {
        if (!newList.contains(installedUuid)) {
            qWarning() << "App ids in order list not matching with installed apps.";
            return;
        }
    }
    if (newList.first() != QUuid(SETTINGS_APP_UUID)) {
        qWarning() << "Settings app must be the first app.";
        return;
    }

    m_appList = newList;
    QSettings settings(m_pebble->storagePath() + "/apps.conf", QSettings::IniFormat);
    QStringList tmp;
    foreach (const QUuid &id, m_appList) {
        tmp << id.toString();
    }
    settings.setValue("appList", tmp);
    emit appsChanged();

    QByteArray data;
    WatchDataWriter writer(&data);
    writer.write<quint8>(0x01);
    writer.write<quint8>(m_appList.count());
    foreach (const QUuid &uuid, m_appList) {
        writer.writeUuid(uuid);
    }

    qDebug() << "writing" << data.toHex();
    m_connection->writeToPebble(WatchConnection::EndpointSorting, data);
}

AppFetchResponse::AppFetchResponse(Status status):
    m_status(status)
{

}

void AppFetchResponse::setStatus(AppFetchResponse::Status status)
{
    m_status = status;
}

QByteArray AppFetchResponse::serialize() const
{
    QByteArray ret;
    WatchDataWriter writer(&ret);
    writer.write<quint8>(m_command);
    writer.write<quint8>(m_status);
    return ret;
}
