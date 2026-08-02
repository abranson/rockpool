/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_NOTIFICATION_MONITOR_H
#define LIBPEBBLE3D_SAILFISH_NOTIFICATION_MONITOR_H

#include <QObject>
#include <QString>

#include <stdint.h>

#include <functional>

class NotificationMonitorPrivate;

class NotificationMonitor : public QObject {
public:
    struct Notification {
        uint32_t flags;
        qint64 timestampMs;
        uint32_t closeReason;
        QString id;
        QString replacesId;
        QString applicationId;
        QString applicationName;
        QString title;
        QString body;
        QString category;
        QString iconName;

        Notification();
    };

    typedef std::function<void(const Notification &)> PostedCallback;
    typedef std::function<void(const Notification &)> ClosedCallback;
    typedef std::function<void(bool)> HealthCallback;

    NotificationMonitor(const PostedCallback &posted,
                        const ClosedCallback &closed,
                        const HealthCallback &health,
                        QObject *parent = 0);
    ~NotificationMonitor();

    bool start();
    int32_t command(uint32_t command, const QString &id);

private:
    NotificationMonitorPrivate *m_private;
};

#endif
