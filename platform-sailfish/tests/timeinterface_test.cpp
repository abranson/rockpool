/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QEventLoop>
#include <QProcess>
#include <QTimer>

#include "timeinterface.h"

int main(int argc, char **argv) {
    QProcess daemon;
    daemon.start(QStringLiteral("dbus-daemon"),
                 QStringList() << QStringLiteral("--session")
                     << QStringLiteral("--nofork")
                     << QStringLiteral("--print-address=1"));
    assert(daemon.waitForReadyRead());
    const QByteArray address = daemon.readLine().trimmed();
    assert(!address.isEmpty());
    qputenv("DBUS_SESSION_BUS_ADDRESS", address);
    QCoreApplication application(argc, argv);
    QDBusConnection bus = QDBusConnection::sessionBus();
    assert(bus.isConnected());
    assert(bus.registerService(QStringLiteral("com.nokia.time")));
    SailfishTimeInterface time(bus);
    int changes = 0;
    QEventLoop loop;
    QObject::connect(&time, &SailfishTimeInterface::settings_changed,
                     &loop, [&changes, &loop]() {
        ++changes;
        loop.quit();
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);

    // timed sends a wall-clock info structure and a time_changed boolean.
    // Accept its payload without needing the custom type, including timezone
    // changes for which time_changed is false.
    QDBusArgument info;
    info.beginStructure();
    info << QStringLiteral("Europe/Helsinki") << qint32(10800);
    info.endStructure();
    QDBusMessage signal = QDBusMessage::createSignal(
        QStringLiteral("/com/nokia/time"), QStringLiteral("com.nokia.time"),
        QStringLiteral("settings_changed"));
    for (int i = 0; i < 2; ++i) {
        signal.setArguments(QVariantList() << QVariant::fromValue(info) << (i != 0));
        assert(bus.send(signal));
        deadline.start(2000);
        loop.exec();
        deadline.stop();
        assert(changes == i + 1);
    }
    assert(bus.unregisterService(QStringLiteral("com.nokia.time")));
    daemon.terminate();
    assert(daemon.waitForFinished());
    return 0;
}
