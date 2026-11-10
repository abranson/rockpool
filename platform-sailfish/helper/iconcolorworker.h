/*
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIBPEBBLE3D_SAILFISH_ICON_COLOR_WORKER_H
#define LIBPEBBLE3D_SAILFISH_ICON_COLOR_WORKER_H

#include <QHash>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>
#include <functional>
#include "wire.h"

class IconColorWorker : public QThread {
    Q_OBJECT
public:
    typedef std::function<void(quint64, int32_t, int32_t)> CompletedCallback;
    explicit IconColorWorker(const CompletedCallback &completed, QObject *parent = 0);
    ~IconColorWorker();
    void query(quint64 id, const lp3wire::IconColorQueryData &query);
    void cancel(quint64 id);

signals:
    void resultReady(quint64 id, quint64 token, int color);

protected:
    void run();

private:
    struct Request { quint64 id; quint64 token; QString source; QString icon; };
    QMutex m_mutex;
    QWaitCondition m_available;
    QQueue<Request> m_queue;
    QHash<quint64, quint64> m_pending;
    quint64 m_nextToken;
    bool m_stopping;
    CompletedCallback m_completed;
};

#endif
