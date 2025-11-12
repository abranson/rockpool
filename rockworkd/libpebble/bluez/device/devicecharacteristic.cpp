#include "devicecharacteristic.h"
#include <functional>
#include <QDebug>

DeviceCharacteristic::DeviceCharacteristic(const QString &path, const QVariantMap &properties, QObject *parent):
  QObject(parent),
  m_properties(properties)
{
    m_iface = new QDBusInterface("org.bluez", path, "org.bluez.GattCharacteristic1", QDBusConnection::systemBus(), this);
    QDBusConnection::systemBus().connect("org.bluez", path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                        this, SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
}

QString DeviceCharacteristic::uuid() const
{
    return m_properties.value("UUID").toString();
}

QByteArray DeviceCharacteristic::readCharacteristic()
{
    QDBusReply<QByteArray> reply = m_iface->call("ReadValue", QVariantMap());
    if (reply.isValid())
        return reply.value();

    return QByteArray();
}

void DeviceCharacteristic::writeCharacteristic(const QByteArray &data)
{
    qWarning() << "Writing a characteristic!" << data.toHex();
    m_iface->asyncCall("WriteValue", data, QVariantMap());
}

void DeviceCharacteristic::propertiesChanged(QString interface, QVariantMap properties, QStringList /*invalidated_properties*/)
{
    if (m_notifyStarted) {
        for (int i = 0; i < m_callbacks.size(); i++) {
            PrvCallback cb = m_callbacks.value(i);
            QByteArray value = properties.value("Value").toByteArray();
            // TODO: Ales
            QMetaObject::invokeMethod(cb.obj.data(), [cb, value]() {
                std::invoke(cb.functor, value);
            }, Qt::QueuedConnection);
        }
    }
}
