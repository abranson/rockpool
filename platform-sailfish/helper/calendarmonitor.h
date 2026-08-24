/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_CALENDAR_MONITOR_H
#define LIBPEBBLE3D_SAILFISH_CALENDAR_MONITOR_H

#include <QByteArray>
#include <QObject>
#include <QThread>

#include <functional>

#include "wire.h"

class CalendarCancelState;
class CalendarWorker;

class CalendarMonitor : public QObject {
    Q_OBJECT

public:
    typedef std::function<void(quint64, int32_t, const QByteArray &)>
        CompletedCallback;
    typedef std::function<void()> ChangedCallback;
    typedef std::function<void(bool)> HealthCallback;

    CalendarMonitor(const CompletedCallback &completed,
                    const ChangedCallback &changed,
                    const HealthCallback &health,
                    QObject *parent = 0);
    ~CalendarMonitor();

    bool start();
    void query(quint64 requestId, const lp3wire::CalendarQueryData &query);
    bool cancel(quint64 requestId);

signals:
    void startRequested();
    void stopRequested();
    void queryRequested(quint64 requestId, quint32 kind, quint32 maxRecords,
                        quint32 offset, qint64 startMs, qint64 endMs,
                        const QString &calendarId);

private:
    QThread m_thread;
    CalendarWorker *m_worker;
    CalendarCancelState *m_cancelState;
    CompletedCallback m_completed;
    ChangedCallback m_changed;
    HealthCallback m_health;
    bool m_started;
    bool m_ready;
};

#endif
