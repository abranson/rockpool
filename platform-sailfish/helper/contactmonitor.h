/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_CONTACT_MONITOR_H
#define LIBPEBBLE3D_SAILFISH_CONTACT_MONITOR_H

#include <QByteArray>
#include <QObject>
#include <QThread>

#include <functional>

#include "wire.h"

class ContactCancelState;
class ContactWorker;

class ContactMonitor : public QObject {
    Q_OBJECT

public:
    typedef std::function<void(quint64, int32_t, const QByteArray &)>
        CompletedCallback;
    typedef std::function<void()> ChangedCallback;
    typedef std::function<void(bool)> HealthCallback;

    ContactMonitor(const CompletedCallback &completed,
                   const ChangedCallback &changed,
                   const HealthCallback &health,
                   QObject *parent = 0);
    ~ContactMonitor();

    bool start();
    void query(quint64 requestId, const lp3wire::ContactQueryData &query);
    bool cancel(quint64 requestId);

signals:
    void startRequested();
    void stopRequested();
    void queryRequested(quint64 requestId, quint32 kind, quint32 maxRecords,
                        quint32 offset, const QString &query);

private:
    QThread m_thread;
    ContactCancelState *m_cancelState;
    ContactWorker *m_worker;
    CompletedCallback m_completed;
    ChangedCallback m_changed;
    HealthCallback m_health;
    bool m_started;
    bool m_ready;
};

#endif
