#include "device.h"
#include "deviceservice.h"
#include <QTimer>

Device::Device(const QString &path, const QVariantMap &info, QObject *parent):
  m_path(path),
  m_info(info),
  QObject(parent)
{
    m_iface = new BluezDevice1(BLUEZ_SERVICE, path, QDBusConnection::systemBus(), this);

    m_connected = m_iface->property("Connected").toBool();
    m_paired = m_iface->property("Paired").toBool();
    m_servicesDiscovered = m_iface->property("ServicesResolved").toBool();

    QDBusConnection::systemBus().connect("org.bluez", path, "org.freedesktop.DBus.Properties", "PropertiesChanged", this, SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
}

Device::~Device()
{
    m_services.clear();
}

void Device::connectToDevice()
{
    m_iface->Connect();
}

void Device::disconnectFromDevice()
{
    m_iface->Disconnect();
}

void Device::pair() const
{
    m_iface->Pair();
}

QString Device::path() const
{
    return m_path;
}

QString Device::name() const
{
    return m_info.value("Name").toString();
}

QBluetoothAddress Device::address() const
{
    return QBluetoothAddress(m_info.value("Address").toString());
}

bool Device::connected() const
{
    return m_connected;
}

bool Device::paired() const
{
    return m_paired;
}

bool Device::servicesDiscovered() const
{
    return m_servicesDiscovered;
}

bool Device::hasService(QString serviceUuid) const
{
    return m_services.contains(serviceUuid);
}

bool Device::hasServices() const
{
    return !m_services.isEmpty();
}

void Device::propertiesChanged(QString interface, QVariantMap properties, QStringList /*invalid_properties*/)
{
    if (interface == "org.bluez.Device1") {
        if (properties.contains("Connected")) {
            qDebug() << "Got connection notification.";
            m_connected = properties.value("Connected").toBool();
            qDebug() << "Are we connected now?" << m_connected;
            emit connectedChanged();

            if (properties.contains("ServicesResolved") || m_servicesDiscovered) {
                emit servicesChanged();
            }
        }

        if (properties.contains("Paired")) {
            m_paired = properties.value("Paired").toBool();
            emit pairedChanged();
        }

        if (properties.contains("ServicesResolved")) {
            m_servicesDiscovered = properties.value("ServicesResolved").toBool();
            if (m_servicesDiscovered)
                emit servicesChanged();
        }
    }
}

void Device::serviceDiscovered(DeviceService *service)
{
    m_services.insert(service->serviceUuid(), service);

    if (m_callbacks.contains(service->serviceUuid())) {
        Callback cb = m_callbacks.take(service->serviceUuid());
        QTimer::singleShot(0, cb.obj.data(), [cb, service]() {
            std::invoke(cb.functor, service);
        });
    }
}
