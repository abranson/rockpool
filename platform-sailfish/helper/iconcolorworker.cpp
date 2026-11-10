/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "iconcolorworker.h"
#include "iconcolor.h"
#include "libpebble3d-platform.h"
#include <QMutexLocker>

IconColorWorker::IconColorWorker(const CompletedCallback &completed, QObject *parent)
    : QThread(parent), m_nextToken(0), m_stopping(false), m_completed(completed)
{
    connect(this, &IconColorWorker::resultReady, this,
            [this](quint64 id, quint64 token, int color) {
        if (m_pending.value(id) != token)
            return;
        m_pending.remove(id);
        m_completed(id, LP3_PLATFORM_OK, color);
    });
    start();
}

IconColorWorker::~IconColorWorker()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stopping = true;
        m_queue.clear();
        m_available.wakeOne();
    }
    wait();
}

void IconColorWorker::query(quint64 id, const lp3wire::IconColorQueryData &query)
{
    QMutexLocker locker(&m_mutex);
    if (m_queue.size() >= 16) {
        locker.unlock();
        m_completed(id, LP3_PLATFORM_BUSY, -1);
        return;
    }
    Request request;
    request.id = id;
    request.token = ++m_nextToken;
    if (!request.token) request.token = ++m_nextToken;
    request.source = QString::fromUtf8(query.source.data(), static_cast<int>(query.source.size()));
    request.icon = QString::fromUtf8(query.icon.data(), static_cast<int>(query.icon.size()));
    m_pending.insert(id, request.token);
    m_queue.enqueue(request);
    m_available.wakeOne();
}

void IconColorWorker::cancel(quint64 id)
{
    m_pending.remove(id);
    QMutexLocker locker(&m_mutex);
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).id == id) m_queue.removeAt(i);
    }
}

void IconColorWorker::run()
{
    for (;;) {
        m_mutex.lock();
        while (m_queue.isEmpty() && !m_stopping) m_available.wait(&m_mutex);
        if (m_stopping) {
            m_mutex.unlock();
            return;
        }
        const Request request = m_queue.dequeue();
        m_mutex.unlock();
        const int32_t color = iconColor(request.source, request.icon);
        emit resultReady(request.id, request.token, color);
    }
}
