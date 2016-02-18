/*
 * Copyright (C) 2013-2015 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 * Charles Kerr <charles.kerr@canonical.com>
 */

#ifndef USS_BLUETOOTH_DEVICE_H
#define USS_BLUETOOTH_DEVICE_H

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QSharedPointer>
#include <QString>

#include "freedesktop_properties.h"
#include "bluez_device1.h"

struct Device: QObject
{
    Q_OBJECT

    Q_PROPERTY(QString path
               READ getPath
               NOTIFY pathChanged)

    Q_PROPERTY(QString name
               READ getName
               NOTIFY nameChanged)

    Q_PROPERTY(QString iconName
               READ getIconName
               NOTIFY iconNameChanged)

    Q_PROPERTY(QString address
               READ getAddress
               NOTIFY addressChanged)

    Q_PROPERTY(Type type
               READ getType
               NOTIFY typeChanged)

    Q_PROPERTY(bool paired
               READ isPaired
               NOTIFY pairedChanged)

    Q_PROPERTY(bool trusted
               READ isTrusted
               WRITE makeTrusted
               NOTIFY trustedChanged)

    Q_PROPERTY(Connection connection
               READ getConnection
               NOTIFY connectionChanged)

    Q_PROPERTY(Strength strength
               READ getStrength
               NOTIFY strengthChanged)

public:

    enum Type { Other, Computer, Cellular, Smartphone, Phone, Modem, Network,
                Headset, Speakers, Headphones, Video, OtherAudio, Joypad,
                Keypad, Keyboard, Tablet, Mouse, Printer, Camera, Carkit, Watch };

    enum Strength { None, Poor, Fair, Good, Excellent };

    enum Connection { Disconnected=1, Connecting=2,
                      Connected=4, Disconnecting=8 };

    Q_ENUMS(Type Strength Connection)

    Q_DECLARE_FLAGS(Connections, Connection)

Q_SIGNALS:
    void pathChanged();
    void nameChanged();
    void iconNameChanged();
    void addressChanged();
    void typeChanged();
    void pairedChanged();
    void trustedChanged();
    void connectionChanged();
    void strengthChanged();
    void deviceChanged(); // catchall for any change
    void pairingDone(bool success);

public:
    const QString& getName() const { return m_name; }
    const QString& getAddress() const { return m_address; }
    const QString& getIconName() const { return m_iconName; }
    Type getType() const { return m_type; }
    bool isPaired() const { return m_paired; }
    bool isTrusted() const { return m_trusted; }
    Connection getConnection() const { return m_connection; }
    Strength getStrength() const { return m_strength; }
    QString getPath() const { return m_bluezDevice ? m_bluezDevice->path() : QString(); }

  private:
    QString m_name;
    QString m_state;
    QString m_address;
    QString m_iconName;
    QString m_fallbackIconName;
    Type m_type = Type::Other;
    bool m_paired = false;
    bool m_trusted = false;
    Connection m_connection = Connection::Disconnected;
    Strength m_strength = Strength::None;
    bool m_isConnected = false;
    bool m_connectAfterPairing = false;
    QScopedPointer<BluezDevice1> m_bluezDevice;
    QScopedPointer<FreeDesktopProperties> m_bluezDeviceProperties;
    bool m_isPairing = false;

  protected:
    void setName(const QString &name);
    void setIconName(const QString &name);
    void setAddress(const QString &address);
    void setType(Type type);
    void setPaired(bool paired);
    void setTrusted(bool trusted);
    void setConnection(Connection connection);
    void setStrength(Strength strength);
    void updateIcon();
    void updateConnection();

  public:
    Device() {}
    Device(const QString &path, QDBusConnection &bus);
    ~Device() {}
    bool isValid() const { return getType() != Type::Other; }
    void pair();
    Q_INVOKABLE void cancelPairing();
    void connect();
    void makeTrusted(bool trusted);
    void disconnect();
    void setProperties(const QMap<QString,QVariant> &properties);
    void setConnectAfterPairing(bool value);

  private Q_SLOTS:
    void slotPropertiesChanged(const QString &interface, const QVariantMap &changedProperties,
                               const QStringList &invalidatedProperties);
    void slotMakeTrustedDone(QDBusPendingCallWatcher *call);

  private:
    void initDevice(const QString &path, QDBusConnection &bus);
    void updateProperties(QSharedPointer<QDBusInterface>);
    void updateProperty(const QString &key, const QVariant &value);
    static Type getTypeFromClass(quint32 bluetoothClass);
    Device::Strength getStrengthFromRssi(int rssi);
    void connectAfterPairing();
};

Q_DECLARE_METATYPE(Device*)

Q_DECLARE_OPERATORS_FOR_FLAGS(Device::Connections)

#endif // USS_BLUETOOTH_DEVICE_H
