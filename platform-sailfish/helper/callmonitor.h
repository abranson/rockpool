/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_CALL_MONITOR_H
#define LIBPEBBLE3D_SAILFISH_CALL_MONITOR_H

#include <QDBusAbstractInterface>
#include <QObject>
#include <QString>

#include <stdint.h>

#include <functional>

class SailfishVoiceCallManagerInterface : public QDBusAbstractInterface {
    Q_OBJECT

public:
    SailfishVoiceCallManagerInterface(const QDBusConnection &connection,
                                      QObject *parent = 0);

Q_SIGNALS:
    void voiceCallsChanged();
};

class SailfishVoiceCallInterface : public QDBusAbstractInterface {
    Q_OBJECT

public:
    SailfishVoiceCallInterface(const QString &path,
                               const QDBusConnection &connection,
                               QObject *parent = 0);

Q_SIGNALS:
    void statusChanged(int status, const QString &handlerId);
    void lineIdChanged(const QString &lineId);
};

class CallMonitorPrivate;

class CallMonitor : public QObject {
public:
    struct Call {
        uint32_t state;
        QString id;
        QString name;
        QString number;

        Call();
    };

    typedef std::function<void(const Call &)> ChangedCallback;
    typedef std::function<void(bool)> HealthCallback;

    CallMonitor(const ChangedCallback &changed,
                const HealthCallback &health,
                QObject *parent = 0);
    ~CallMonitor();

    bool start();
    int32_t command(uint32_t command, const QString &id);

private:
    CallMonitorPrivate *m_private;
};

#endif
