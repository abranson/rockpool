#ifndef DEVICESERVICE_H
#define DEVICESERVICE_H

#include <QObject>
#include <QMap>
#include <QVariant>
#include <QDBusInterface>
#include <QDBusReply>
#include <QEventLoop>

class DeviceCharacteristic;

class DeviceService : public QObject
{
    Q_OBJECT

public:
    DeviceService(const QString &path, const QVariantMap &properties, QObject *parent = nullptr);
    ~DeviceService();

    QString serviceUuid() const;

    QString devicePath() const;

    DeviceCharacteristic *characteristic(const QString &uuid);

    // Called by dbus watcher
    void addCharacteristic(DeviceCharacteristic *characteristic);

signals:
    void characteristicAdded(QString uuid);

private:
    QDBusInterface *m_objectIface;
    QString m_path;
    QVariantMap m_info;

    // Hash of UUID and path
    QHash<QString, DeviceCharacteristic*> m_characteristics;

    Q_DISABLE_COPY(DeviceService)
};

#endif // DEVICESERVICE_H
