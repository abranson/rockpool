/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LIBPEBBLE3D_SAILFISH_PHONE_STATE_H
#define LIBPEBBLE3D_SAILFISH_PHONE_STATE_H

#include <QDBusAbstractInterface>
#include <QDBusConnection>
#include <QObject>
#include <QTimer>
#include <functional>
#include <stdint.h>

class QDBusServiceWatcher;

class PrivacyInterface : public QDBusAbstractInterface {
    Q_OBJECT
public:
    PrivacyInterface(const QString &owner, const QDBusConnection &bus, QObject *parent);
signals:
    void privacyModeActiveChanged(bool active);
};

class ActivityInterface : public QDBusAbstractInterface {
    Q_OBJECT
public:
    ActivityInterface(const QString &owner, const QDBusConnection &bus, QObject *parent);
signals:
    void system_inactivity_ind(bool inactive);
};

class PhoneState : public QObject {
public:
    typedef std::function<void(uint32_t)> ChangedCallback;
    typedef std::function<void(quint64, uint16_t, int32_t)> CompletedCallback;
    PhoneState(const ChangedCallback &changed, const CompletedCallback &completed,
               QObject *parent = 0);
    ~PhoneState();
    void start();
    uint32_t flags() const;
    void command(quint64 id, uint16_t operation, const QString &value);
    void cancel(quint64 id);

private:
    struct Source {
        Source(const char *connection, const char *service, const char *path,
               const char *interface, const char *method, bool system);
        QDBusConnection bus;
        QString connection, service, path, interface, method, owner;
        bool system, known, active, lookupPending, snapshotPending;
        quint64 generation, version;
        QDBusServiceWatcher *watcher;
        QObject *signalInterface;
    };
    void maintain(Source &source);
    void connectBus(Source &source);
    void lookup(Source &source);
    void setOwner(Source &source, const QString &owner);
    void snapshot(Source &source);
    void accept(Source &source, bool active);
    void publish();
    Source m_privacy;
    Source m_activity;
    QTimer m_timer;
    ChangedCallback m_changed;
    CompletedCallback m_completed;
    bool m_started;
};
#endif
