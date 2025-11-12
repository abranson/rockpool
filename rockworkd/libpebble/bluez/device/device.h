#ifndef DEVICE_H
#define DEVICE_H

#include <QObject>
#include <QBluetoothAddress>
#include <QPointer>
#include <QMetaMethod>
#include <QDebug>

#include "deviceservice.h"
#include "devicecharacteristic.h"
#include "../bluez_device1.h"
#include "../dbus-shared.h"

Q_DECLARE_METATYPE(DeviceService*)

class Device : public QObject
{
    Q_OBJECT
public:
    Device(const QString &path, const QVariantMap &info, QObject *parent = nullptr);
    ~Device();

    void connectToDevice();
    void disconnectFromDevice();
    void pair() const;

    QString path() const;
    QString name() const;
    QBluetoothAddress address() const;

    bool connected() const;
    bool paired() const;
    bool servicesDiscovered() const;

    bool hasService(QString serviceUuid) const;
    bool hasServices() const;

    template <typename Object, typename Func>
    void getService(const QString &serviceUuid, Object obj, Func functor) {
        if (m_services.contains(serviceUuid.toLower())) {
            std::invoke(functor, obj, m_services.value(serviceUuid.toLower()));
        }

        Callback cb;
        cb.obj = obj;
        cb.functor = std::bind(functor, obj, std::placeholders::_1);
        m_callbacks.insert(serviceUuid, cb);
    }

    // Called by dbus watcher
    void serviceDiscovered(DeviceService *service);

signals:
    void connectedChanged();
    void pairedChanged();
    void servicesChanged();

private slots:
    void propertiesChanged(QString interface, QVariantMap properties, QStringList /*invalid_properties*/);

private:
    BluezDevice1 *m_iface;
    QString m_path;
    QVariantMap m_info;

    // Each service is associated with a UUID
    QHash<QString, DeviceService*> m_services;

    class Callback {
    public:
        QPointer<QObject> obj;
        std::function<void(DeviceService*)> functor;
    };
    QMap<QString, Callback> m_callbacks;

    bool m_connected = false;
    bool m_paired = false;
    bool m_servicesDiscovered = false;
};

#endif // DEVICE_H
