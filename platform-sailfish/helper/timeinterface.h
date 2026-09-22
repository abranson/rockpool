/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_TIME_INTERFACE_H
#define LIBPEBBLE3D_SAILFISH_TIME_INTERFACE_H

#include <QDBusAbstractInterface>
#include <QDBusConnection>

class SailfishTimeInterface : public QDBusAbstractInterface {
    Q_OBJECT

public:
    explicit SailfishTimeInterface(const QDBusConnection &connection,
                                   QObject *parent = 0)
        : QDBusAbstractInterface(QStringLiteral("com.nokia.time"),
                                 QStringLiteral("/com/nokia/time"),
                                 "com.nokia.time", connection, parent) {}

Q_SIGNALS:
    // Only the notification is needed, not timed's wall-clock info payload.
    void settings_changed();
};

#endif
