#include "deviceservice.h"
#include "devicecharacteristic.h"
#include "../bluez_helper.h"

DeviceService::DeviceService(const QString &path, const QVariantMap &properties, QObject *parent):
  QObject(parent),
  m_path(path),
  m_info(properties)
{
    m_objectIface = new QDBusInterface("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", QDBusConnection::systemBus(), this);
}

DeviceService::~DeviceService()
{
    m_characteristics.clear();
}

QString DeviceService::serviceUuid() const
{
    return m_info.value("UUID").toString();
}

QString DeviceService::devicePath() const
{
    return qvariant_cast<QDBusObjectPath>(m_info.value("Device")).path();
}

DeviceCharacteristic *DeviceService::characteristic(const QString &uuid)
{
    if (!m_characteristics.contains(uuid.toLower())) {

        // TODO: Somehow optimize this with BluezClient and other classes
        QDBusReply<ManagedObjectList> reply = m_objectIface->call("GetManagedObjects");
        const QString prefix = "/char";

        for (auto it = reply.value().constBegin(), end = reply.value().constEnd(); it != end; ++it) {
            const QString path = it.key().path();
            if (path.startsWith(m_path + prefix) && it.value().contains("org.bluez.GattCharacteristic1")) {
                DeviceCharacteristic *characteristic = new DeviceCharacteristic(path, it.value().value("org.bluez.GattCharacteristic1"), this);
                addCharacteristic(characteristic);
            }
        }

        // By this point we have all characteristics added

        if (m_characteristics.contains(uuid.toLower()))
            return m_characteristics.value(uuid.toLower());
        else
            return nullptr;
    }

    return m_characteristics.value(uuid.toLower());
}

void DeviceService::addCharacteristic(DeviceCharacteristic *characteristic)
{
    m_characteristics.insert(characteristic->uuid(), characteristic);
    emit characteristicAdded(characteristic->uuid());
}