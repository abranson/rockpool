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

#include "device.h"

#include <QDBusReply>
#include <QDebug> // qWarning()
#include <QThread>
#include <QTimer>

#include "dbus-shared.h"

Device::Device(const QString &path, QDBusConnection &bus) :
   m_strength(Device::None)
{
    initDevice(path, bus);
}

void Device::initDevice(const QString &path, QDBusConnection &bus)
{
    /* whenever any of the properties changes,
       trigger the catch-all deviceChanged() signal */
    QObject::connect(this, SIGNAL(nameChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(iconNameChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(addressChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(pairedChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(trustedChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(typeChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(connectionChanged()), this, SIGNAL(deviceChanged()));
    QObject::connect(this, SIGNAL(strengthChanged()), this, SIGNAL(deviceChanged()));

    m_bluezDevice.reset(new BluezDevice1(BLUEZ_SERVICE, path, bus));
    /* Give our calls a bit more time than the default 25 seconds to
     * complete whatever they are doing. In some situations (e.g. with
     * specific devices) the default doesn't seem to be enough to. */
    m_bluezDevice->setTimeout(60 * 1000 /* 60 seconds */);

    m_bluezDeviceProperties.reset(new FreeDesktopProperties(BLUEZ_SERVICE, path, bus));

    QObject::connect(m_bluezDeviceProperties.data(), SIGNAL(PropertiesChanged(const QString&, const QVariantMap&, const QStringList&)),
                     this, SLOT(slotPropertiesChanged(const QString&, const QVariantMap&, const QStringList&)));

    Q_EMIT(pathChanged());

    watchCall(m_bluezDeviceProperties->GetAll(BLUEZ_DEVICE_IFACE), [=](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<QVariantMap> reply = *watcher;

        if (reply.isError()) {
            qWarning() << "Failed to retrieve properties for device" << m_bluezDevice->path();
            watcher->deleteLater();
            return;
        }

        auto properties = reply.argumentAt<0>();
        setProperties(properties);

        watcher->deleteLater();
    });
}

void Device::slotPropertiesChanged(const QString &interface, const QVariantMap &changedProperties,
                                   const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties);

   if (interface != BLUEZ_DEVICE_IFACE)
      return;

   setProperties(changedProperties);
}

void Device::setProperties(const QMap<QString,QVariant> &properties)
{
    QMapIterator<QString,QVariant> it(properties);
    while (it.hasNext()) {
        it.next();
        updateProperty(it.key(), it.value());
    }
}

void Device::setConnectAfterPairing(bool value)
{
    if (m_connectAfterPairing == value)
        return;

    m_connectAfterPairing = value;
}

void Device::disconnect()
{
    setConnection(Device::Disconnecting);

    QDBusPendingCall call = m_bluezDevice->Disconnect();

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<void> reply = *watcher;

        if (reply.isError()) {
            qWarning() << "Could not disconnect device:"
                       << reply.error().message();

            // Make sure we switch the connection indicator back to
            // a sane state
            updateConnection();
        }

        watcher->deleteLater();
    });
}

void Device::connectAfterPairing()
{
    if (!m_connectAfterPairing)
        return;

    connect();
}

void Device::pair()
{
    if (m_paired) {
        // If we are already paired we just have to make sure we
        // trigger the connection process if we have to
        connectAfterPairing();
        return;
    }

    setConnection(Device::Connecting);

    m_isPairing = true;

    auto call = m_bluezDevice->asyncCall("Pair");

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<void> reply = *watcher;
        bool success = true;

        if (reply.isError()) {
            qWarning() << "Failed to pair with device:"
                       << reply.error().message();
            updateConnection();
            success = false;
        }

        m_isPairing = false;

        Q_EMIT(pairingDone(success));

        watcher->deleteLater();
    });
}

void Device::cancelPairing()
{
   if (!m_isPairing)
      return;

    auto call = m_bluezDevice->asyncCall("CancelPairing");

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<void> reply = *watcher;

        if (reply.isError()) {
            qWarning() << "Failed to cancel pairing attempt with device:"
                       << reply.error().message();
            updateConnection();
        } else {
            // Only mark us a not pairing when call succeeded
            m_isPairing = false;
        }

        watcher->deleteLater();
    });
}

void Device::connect()
{
    // If we have just paired then the device switched to connected = true for
    // a short moment as BlueZ opened up a RFCOMM channel to perform SDP. If
    // we should connect with the device on specific profiles now we go ahead
    // here even if we're marked as connected as this still doesn't mean we're
    // connected on any profile. Calling org.bluez.Device1.Connect multiple
    // times doesn't hurt an will not fail.
    if (m_isConnected && !m_connectAfterPairing)
       return;

    setConnection(Device::Connecting);

    QDBusPendingCall call = m_bluezDevice->asyncCall("Connect");

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<void> reply = *watcher;

        if (reply.isError()) {
            qWarning() << "Could not connect device:"
                       << reply.error().message();
        } else {
            makeTrusted(true);
        }

        // Regardless if the Connected property has changed or not we update
        // the connection state here as the connection process is over now
        // and we should have received any state change already at this
        // point.
        updateConnection();

        watcher->deleteLater();
    });
}

void Device::slotMakeTrustedDone(QDBusPendingCallWatcher *call)
{
    QDBusPendingReply<void> reply = *call;

    if (reply.isError()) {
        qWarning() << "Could not mark device as trusted:"
                   << reply.error().message();
    }

    call->deleteLater();
}

void Device::makeTrusted(bool trusted)
{
    auto call = m_bluezDeviceProperties->Set(BLUEZ_DEVICE_IFACE, "Trusted", QDBusVariant(trusted));

    auto watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
                     this, SLOT(slotMakeTrustedDone(QDBusPendingCallWatcher*)));
}

void Device::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        Q_EMIT(nameChanged());
    }
}

void Device::setIconName(const QString &iconName)
{
    if (m_iconName != iconName) {
        m_iconName = iconName;
        Q_EMIT(iconNameChanged());
    }
}

void Device::setAddress(const QString &address)
{
    if (m_address != address) {
        m_address = address;
        Q_EMIT(addressChanged());
    }
}

void Device::setType(Type type)
{
    if (m_type != type) {
        m_type = type;
        Q_EMIT(typeChanged());
        updateIcon();
    }
}

void Device::setPaired(bool paired)
{
    if (m_paired != paired) {
        m_paired = paired;
        Q_EMIT(pairedChanged());
    }
}

void Device::setTrusted(bool trusted)
{
    if (m_trusted != trusted) {
        m_trusted = trusted;
        Q_EMIT(trustedChanged());
    }
}

void Device::setConnection(Connection connection)
{
    if (m_connection != connection) {
        m_connection = connection;
        Q_EMIT(connectionChanged());
    }
}

void Device::updateIcon()
{
    /* bluez-provided icon is unreliable? In testing I'm getting
       an "audio-card" icon from bluez for my NoiseHush N700 headset.
       Try to guess the icon from the device type,
       and use the bluez-provided icon as a fallback */

    const auto type = getType();

    switch (type) {
    case Type::Headset:
        setIconName("image://theme/audio-headset-symbolic");
        break;
    case Type::Headphones:
        setIconName("image://theme/audio-headphones-symbolic");
        break;
    case Type::Carkit:
        setIconName("image://theme/audio-carkit-symbolic");
        break;
    case Type::Speakers:
    case Type::OtherAudio:
        setIconName("image://theme/audio-speakers-symbolic");
        break;
    case Type::Mouse:
        setIconName("image://theme/input-mouse-symbolic");
        break;
    case Type::Keyboard:
        setIconName("image://theme/input-keyboard-symbolic");
        break;
    case Type::Cellular:
        setIconName("image://theme/phone-cellular-symbolic");
        break;
    case Type::Smartphone:
        setIconName("image://theme/phone-smartphone-symbolic");
        break;
    case Type::Phone:
        setIconName("image://theme/phone-uncategorized-symbolic");
        break;
    case Type::Computer:
        setIconName("image://theme/computer-symbolic");
        break;
    default:
        setIconName(QString("image://theme/%1").arg(m_fallbackIconName));
    }
}

void Device::updateConnection()
{
    Connection c;

    c = m_isConnected ? Connection::Connected : Connection::Disconnected;

    setConnection(c);
}

void Device::updateProperty(const QString &key, const QVariant &value)
{
    if (key == "Name") {
        setName(value.toString());
    } else if (key == "Address") {
        setAddress(value.toString());
    } else if (key == "Connected") {
        m_isConnected = value.toBool();
        updateConnection();
    } else if (key == "Class") {
        setType(getTypeFromClass(value.toUInt()));
    } else if (key == "Paired") {
        setPaired(value.toBool());

        if (m_paired && m_connectAfterPairing) {
           connectAfterPairing();
           return;
        }

        updateConnection();
    } else if (key == "Trusted") {
        setTrusted(value.toBool());
    } else if (key == "Icon") {
        m_fallbackIconName = value.toString();
        updateIcon ();
    } else if (key == "RSSI") {
        m_strength = getStrengthFromRssi(value.toInt());
        Q_EMIT(strengthChanged());
    }
}

/* Determine the Type from the bits in the Class of Device (CoD) field.
   https://www.bluetooth.org/en-us/specification/assigned-numbers/baseband */
Device::Type Device::getTypeFromClass (quint32 c)
{
    switch ((c & 0x1f00) >> 8) {
    case 0x01:
        return Type::Computer;

    case 0x02:
        switch ((c & 0xfc) >> 2) {
        case 0x01:
            return Type::Cellular;
        case 0x03:
            return Type::Smartphone;
        case 0x04:
            return Type::Modem;
        default:
            return Type::Phone;
        }
        break;

    case 0x03:
        return Type::Network;

    case 0x04:
        switch ((c & 0xfc) >> 2) {
        case 0x01:
        case 0x02:
            return Type::Headset;

        case 0x05:
            return Type::Speakers;

        case 0x06:
            return Type::Headphones;

        case 0x08:
            return Type::Carkit;

        case 0x0b: // vcr
        case 0x0c: // video camera
        case 0x0d: // camcorder
            return Type::Video;

        default:
            return Type::OtherAudio;
        }
        break;

    case 0x05:
        switch ((c & 0xc0) >> 6) {
        case 0x00:
            switch ((c & 0x1e) >> 2) {
            case 0x01:
            case 0x02:
                return Type::Joypad;
            }
            break;

        case 0x01:
            return Type::Keyboard;

        case 0x02:
            switch ((c & 0x1e) >> 2) {
            case 0x05:
                return Type::Tablet;
            default:
                return Type::Mouse;
            }
        }
        break;

    case 0x06:
        if ((c & 0x80) != 0)
            return Type::Printer;
        if ((c & 0x20) != 0)
            return Type::Camera;
        break;

    case 0x07:
        if ((c & 0x4) != 0)
            return Type::Watch;
        break;
    }

    return Type::Other;
}

Device::Strength Device::getStrengthFromRssi(int rssi)
{
    /* Modelled similar to what Mac OS X does.
     * See http://www.cnet.com/how-to/how-to-check-bluetooth-connection-strength-in-os-x/ */

    if (rssi >= -60)
        return Excellent;
    else if (rssi < -60 && rssi >= -70)
        return Good;
    else if (rssi < -70 && rssi >= -90)
        return Fair;
    else if (rssi < -90)
        return Poor;

    return None;
}
