#ifndef BLUEZCLIENT_H
#define BLUEZCLIENT_H

#include <QList>
#include <QBluetoothAddress>
#include <QBluetoothLocalDevice>

#include "bluez_helper.h"
#include "freedesktop_objectmanager.h"
#include "freedesktop_properties.h"
#include "bluez_adapter1.h"
#include "bluez_agentmanager1.h"

#include "device/device.h"

class BluezClient: public QObject
{
    Q_OBJECT

public:
    BluezClient(QObject *parent = 0);

    Device *getDevice(const QBluetoothAddress &address);

    QList<Device*> pairedPebbles() const;

private slots:
    void addDevice(const QDBusObjectPath &path, const QVariantMap &properties);

    void slotInterfacesAdded(const QDBusObjectPath&path, InterfaceList ifaces);
    void slotInterfacesRemoved(const QDBusObjectPath&path, const QStringList &ifaces);

signals:
    void devicesChanged();

private:
    QDBusConnection m_dbus;
    DBusObjectManagerInterface m_bluezManager;
    BluezAgentManager1 m_bluezAgentManager;
    BluezAdapter1 *m_bluezAdapter = nullptr;
    FreeDesktopProperties *m_bluezAdapterProperties = nullptr;


    QHash<QString, Device*> m_devices;
    QHash<QString, DeviceService*> m_services;
    QHash<QString, DeviceCharacteristic*> m_characteristics;
};

#endif // BLUEZCLIENT_H
