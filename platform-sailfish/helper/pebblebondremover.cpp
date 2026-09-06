/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pebblebondremover.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QRegExp>
#include <QVariant>
#include <QVariantMap>

#include "libpebble3d-platform.h"

namespace {

const char kBluezService[] = "org.bluez";
const char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";
const char kDeviceInterface[] = "org.bluez.Device1";
const char kAdapterInterface[] = "org.bluez.Adapter1";
const char kClassicPebbleUuid[] = "00000000-deca-fade-deca-deafdecacaff";
const char kLePebbleUuid[] = "0000fed9-0000-1000-8000-00805f9b34fb";
const int kDbusTimeoutMs = 1500;

QVariant unwrapped(const QVariant &value) {
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

bool isMissingObjectError(const QString &name) {
    return name == QStringLiteral("org.freedesktop.DBus.Error.UnknownObject") ||
        name == QStringLiteral("org.bluez.Error.DoesNotExist");
}

QString adapterPath(quint32 adapterIndex) {
    return QStringLiteral("/org/bluez/hci%1").arg(adapterIndex);
}

QString addressString(const uint8_t address[6]) {
    QStringList octets;
    for (size_t index = 0; index < 6; ++index) {
        octets.append(QStringLiteral("%1").arg(
            address[index], 2, 16, QLatin1Char('0')));
    }
    return octets.join(QLatin1Char(':')).toUpper();
}

QString devicePath(const QString &adapter, const QString &address) {
    QString suffix = address;
    suffix.replace(QLatin1Char(':'), QLatin1Char('_'));
    return adapter + QStringLiteral("/dev_") + suffix;
}

QStringList stringList(const QVariant &value) {
    const QVariant plain = unwrapped(value);
    if (plain.canConvert<QStringList>()) {
        return plain.toStringList();
    }
    return QStringList();
}

} // namespace

namespace sailfishplatform {

bool isRecognizedPebbleDevice(const QString &name, const QStringList &uuids,
                              bool hasBluetoothClass,
                              quint32 bluetoothClass) {
    const QRegExp pebbleName(
        QStringLiteral("^Pebble(?: Time(?: Le)?)? [0-9A-Fa-f]{4}$"));
    if (!pebbleName.exactMatch(name)) {
        return false;
    }
    for (QStringList::const_iterator uuid = uuids.constBegin();
         uuid != uuids.constEnd(); ++uuid) {
        if (uuid->compare(QString::fromLatin1(kClassicPebbleUuid),
                          Qt::CaseInsensitive) == 0 ||
            uuid->compare(QString::fromLatin1(kLePebbleUuid),
                          Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return (hasBluetoothClass && bluetoothClass != 0) ||
        name.startsWith(QStringLiteral("Pebble Time Le "),
                        Qt::CaseInsensitive);
}

int removePebbleBond(const lp3wire::PebbleBondRemoveData &request) {
    if (!lp3wire::validPebbleBondRemove(request)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    const QString adapter = adapterPath(request.adapterIndex);
    const QString address = addressString(request.address);
    const QString device = devicePath(adapter, address);
    const QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return LP3_PLATFORM_UNAVAILABLE;
    }

    QDBusMessage getProperties = QDBusMessage::createMethodCall(
        QString::fromLatin1(kBluezService), device,
        QString::fromLatin1(kPropertiesInterface), QStringLiteral("GetAll"));
    getProperties << QString::fromLatin1(kDeviceInterface);
    const QDBusReply<QVariantMap> propertiesReply = bus.call(
        getProperties, QDBus::Block, kDbusTimeoutMs);
    if (!propertiesReply.isValid()) {
        return isMissingObjectError(propertiesReply.error().name()) ?
            LP3_PLATFORM_OK : LP3_PLATFORM_IO_ERROR;
    }

    const QVariantMap properties = propertiesReply.value();
    if (unwrapped(properties.value(QStringLiteral("Address"))).toString()
            .compare(address, Qt::CaseInsensitive) != 0 ||
        unwrapped(properties.value(QStringLiteral("Adapter")))
                .value<QDBusObjectPath>().path() != adapter) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    const bool paired =
        unwrapped(properties.value(QStringLiteral("Paired"))).toBool();
    const bool bonded =
        unwrapped(properties.value(QStringLiteral("Bonded"))).toBool();
    if (!paired && !bonded) {
        return LP3_PLATFORM_OK;
    }

    QString name =
        unwrapped(properties.value(QStringLiteral("Name"))).toString();
    if (name.isEmpty()) {
        name = unwrapped(properties.value(QStringLiteral("Alias"))).toString();
    }
    bool hasBluetoothClass = false;
    const quint32 bluetoothClass =
        unwrapped(properties.value(QStringLiteral("Class")))
            .toUInt(&hasBluetoothClass);
    if (!isRecognizedPebbleDevice(
            name, stringList(properties.value(QStringLiteral("UUIDs"))),
            hasBluetoothClass, bluetoothClass)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    QDBusMessage remove = QDBusMessage::createMethodCall(
        QString::fromLatin1(kBluezService), adapter,
        QString::fromLatin1(kAdapterInterface), QStringLiteral("RemoveDevice"));
    remove << QVariant::fromValue(QDBusObjectPath(device));
    const QDBusMessage removeReply = bus.call(
        remove, QDBus::Block, kDbusTimeoutMs);
    if (removeReply.type() == QDBusMessage::ReplyMessage) {
        return LP3_PLATFORM_OK;
    }
    return isMissingObjectError(removeReply.errorName()) ?
        LP3_PLATFORM_OK : LP3_PLATFORM_IO_ERROR;
}

} // namespace sailfishplatform
