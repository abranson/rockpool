#include "bluezclient.h"
#include "dbus-shared.h"

#include <QDBusConnection>
#include <QDBusReply>
#include <QDebug>

BluezClient::BluezClient(QObject *parent):
    QObject(parent),
    m_dbus(QDBusConnection::systemBus()),
    m_bluezManager("org.bluez", "/", m_dbus),
    m_bluezAgentManager("org.bluez", "/org/bluez", m_dbus)
{
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();

    if (m_bluezManager.isValid()) {
        connect(&m_bluezManager, SIGNAL(InterfacesAdded(const QDBusObjectPath&, InterfaceList)),
                this, SLOT(slotInterfacesAdded(const QDBusObjectPath&, InterfaceList)));

        connect(&m_bluezManager, SIGNAL(InterfacesRemoved(const QDBusObjectPath&, const QStringList&)),
                this, SLOT(slotInterfacesRemoved(const QDBusObjectPath&, const QStringList&)));

        auto objectList = m_bluezManager.GetManagedObjects().argumentAt<0>();
        for (QDBusObjectPath path : objectList.keys()) {
            InterfaceList ifaces = objectList.value(path);
            if (ifaces.contains(BLUEZ_DEVICE_IFACE)) {
                slotInterfacesAdded(path, ifaces);
            }
        }

        if (m_devices.isEmpty()) {
            // Try with bluez 4
            QDBusConnection system = QDBusConnection::systemBus();

            QDBusReply<QList<QDBusObjectPath> > listAdaptersReply = system.call(
                        QDBusMessage::createMethodCall("org.bluez", "/", "org.bluez.Manager",
                                                       "ListAdapters"));
            if (!listAdaptersReply.isValid()) {
                qWarning() << listAdaptersReply.error().message();
                return;
            }

            QList<QDBusObjectPath> adapters = listAdaptersReply.value();

            if (adapters.isEmpty()) {
                qWarning() << "No BT adapters found";
                return;
            }

            QDBusReply<QVariantMap> adapterPropertiesReply = system.call(
                        QDBusMessage::createMethodCall("org.bluez", adapters[0].path(), "org.bluez.Adapter",
                                                       "GetProperties"));
            if (!adapterPropertiesReply.isValid()) {
                qWarning() << adapterPropertiesReply.error().message();
                return;
            }

            QList<QDBusObjectPath> devices;
            adapterPropertiesReply.value()["Devices"].value<QDBusArgument>() >> devices;

            foreach (QDBusObjectPath path, devices) {
                QDBusReply<QVariantMap> devicePropertiesReply = system.call(
                            QDBusMessage::createMethodCall("org.bluez", path.path(), "org.bluez.Device",
                                                           "GetProperties"));
                if (!devicePropertiesReply.isValid()) {
                    qCritical() << devicePropertiesReply.error().message();
                    continue;
                }

                const QVariantMap &dict = devicePropertiesReply.value();

                QString name = dict["Name"].toString();
                if (name.startsWith("Pebble") && !name.startsWith("Pebble Time LE") && !name.startsWith("Pebble-LE")) {
                    qDebug() << "Found Pebble:" << name;
                    addDevice(path, dict);
                }
            }
        }
    }
}

Device *BluezClient::getDevice(const QBluetoothAddress &address)
{
    foreach (Device *device, m_devices) {
        if (device->address() == address)
            return device;
    }

    return nullptr;
}

QList<Device*> BluezClient::pairedPebbles() const
{
    QList<Device*> ret;
    if (m_bluezManager.isValid()) {
        foreach (Device *dev, m_devices) {
            ret << dev;
        }
    }
    return ret;
}

void BluezClient::addDevice(const QDBusObjectPath &path, const QVariantMap &properties)
{
    QString address = properties.value("Address").toString();
    QString name = properties.value("Name").toString();
    if (name.startsWith("Pebble") && !name.startsWith("Pebble Time LE") && !name.startsWith("Pebble-LE") && !m_devices.contains(path.path())) {
        qDebug() << "Found new Pebble:" << name;
        Device *device = new Device(path.path(), properties, this);

        m_devices.insert(path.path(), device);
        emit devicesChanged();
    }
}

void BluezClient::slotInterfacesAdded(const QDBusObjectPath &path, InterfaceList ifaces)
{
    qDebug() << "Interface added!";
    if (ifaces.contains(BLUEZ_DEVICE_IFACE)) {
        auto properties = ifaces.value(BLUEZ_DEVICE_IFACE);
        addDevice(path, properties);
    }

    if (ifaces.contains(BLUEZ_SERVICE_IFACE)) {
        auto properties = ifaces.value(BLUEZ_SERVICE_IFACE);
        QString devicePath = qvariant_cast<QDBusObjectPath>(properties.value("Device")).path();
        if (m_devices.contains(devicePath)) {
            Device *device = m_devices.value(devicePath);
            DeviceService *service = new DeviceService(path.path(), properties, device);
            device->serviceDiscovered(service);
            m_services.insert(path.path(), service);
        }
    }

    if (ifaces.contains(BLUEZ_CHARACTERISTIC_IFACE)) {
        auto properties = ifaces.value(BLUEZ_CHARACTERISTIC_IFACE);
        QString servicePath = qvariant_cast<QDBusObjectPath>(properties.value("Service")).path();
        if (m_services.contains(servicePath)) {
            DeviceService *service = m_services.value(servicePath);
            DeviceCharacteristic *characteristic = new DeviceCharacteristic(path.path(), properties, service);
            service->addCharacteristic(characteristic);
            m_characteristics.insert(path.path(), characteristic);
        }
    }

}

void BluezClient::slotInterfacesRemoved(const QDBusObjectPath &path, const QStringList &ifaces)
{
    // TODO: Maybe clean up individual services and characteristics too?
    qDebug() << "interfaces removed" << path.path() << ifaces;
    if (!ifaces.contains(BLUEZ_DEVICE_IFACE)) {
        return;
    }

    if (m_devices.contains(path.path())) {
        Device *device = m_devices.take(path.path());
        device->deleteLater();
        qDebug() << "removing dev";
        emit devicesChanged();
    }
}
