/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "locationmonitor.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QList>
#include <QMap>
#include <QTimer>
#include <QtMath>

#include <cmath>
#include <limits>

#include "libpebble3d-platform.h"

namespace {

const int kDefaultRetryIntervalMs = 1000;

QGeoPositionInfoSource *createDefaultSource(QObject *parent) {
    return QGeoPositionInfoSource::createDefaultSource(parent);
}

bool finiteValue(double value) {
    return std::isfinite(value);
}

} // namespace

LocationMonitor::Fix::Fix()
    : latitudeE7(0), longitudeE7(0), accuracyM(0), timestampMs(0) {}

class LocationMonitorPrivate {
public:
    struct Request {
        bool highAccuracy;
        qint64 deadlineMs;
    };

    typedef std::function<QGeoPositionInfoSource *(QObject *)> SourceFactory;

    LocationMonitorPrivate(LocationMonitor *owner,
                           const LocationMonitor::CompletedCallback &completed,
                           const LocationMonitor::HealthCallback &health,
                           const SourceFactory &factory,
                           int retryIntervalMs)
        : q(owner), completedCallback(completed), healthCallback(health),
          sourceFactory(factory), source(NULL), started(false), ready(false),
          updatesActive(false), sourceGeneration(0) {
        elapsed.start();
        retryTimer.setSingleShot(true);
        retryTimer.setInterval(retryIntervalMs);
        QObject::connect(&retryTimer, &QTimer::timeout, q, [this]() {
            ensureSource();
        });
        deadlineTimer.setSingleShot(true);
        QObject::connect(&deadlineTimer, &QTimer::timeout, q, [this]() {
            expireRequests();
        });
    }

    ~LocationMonitorPrivate() {
        started = false;
        retryTimer.stop();
        deadlineTimer.stop();
        requests.clear();
        destroySource();
    }

    bool start() {
        if (!started) {
            started = true;
            ensureSource();
        }
        return ready;
    }

    void query(quint64 requestId, bool highAccuracy, uint32_t timeoutMs) {
        if (!started) {
            start();
        }
        if (requests.contains(requestId)) {
            complete(requestId, LP3_PLATFORM_INVALID_ARGUMENT,
                     LocationMonitor::Fix());
            return;
        }
        if (source == NULL && !ensureSource()) {
            complete(requestId, LP3_PLATFORM_UNAVAILABLE,
                     LocationMonitor::Fix());
            return;
        }

        Request request;
        request.highAccuracy = highAccuracy;
        request.deadlineMs = elapsed.elapsed() + timeoutMs;
        requests.insert(requestId, request);
        updatePreferredMethods();
        if (!updatesActive) {
            source->startUpdates();
            updatesActive = true;
        }
        scheduleDeadline();
    }

    bool cancel(quint64 requestId) {
        if (requests.remove(requestId) == 0) {
            return false;
        }
        if (requests.isEmpty()) {
            stopUpdates();
            deadlineTimer.stop();
        } else {
            updatePreferredMethods();
            scheduleDeadline();
        }
        return true;
    }

    int pendingForTest() const { return requests.size(); }
    bool updatesActiveForTest() const { return updatesActive; }

private:
    bool ensureSource() {
        if (!started || source != NULL) {
            return source != NULL;
        }
        QGeoPositionInfoSource *candidate = sourceFactory(q);
        if (candidate == NULL ||
            candidate->supportedPositioningMethods() ==
                QGeoPositionInfoSource::NoPositioningMethods) {
            if (candidate != NULL) {
                candidate->deleteLater();
            }
            setReady(false);
            scheduleRetry();
            return false;
        }

        source = candidate;
        const quint64 generation = ++sourceGeneration;
        QObject::connect(
            source, &QGeoPositionInfoSource::positionUpdated, q,
            [this, generation](const QGeoPositionInfo &position) {
                if (generation == sourceGeneration && source != NULL) {
                    acceptPosition(position);
                }
            });
        QObject::connect(
            source, &QGeoPositionInfoSource::updateTimeout, q,
            [this, generation]() {
                if (generation == sourceGeneration && source != NULL) {
                    expireRequests();
                }
            });
        QObject::connect(
            source,
            static_cast<void (QGeoPositionInfoSource::*)(
                QGeoPositionInfoSource::Error)>(
                    &QGeoPositionInfoSource::error),
            q,
            [this, generation](QGeoPositionInfoSource::Error error) {
                if (generation == sourceGeneration && source != NULL &&
                    error != QGeoPositionInfoSource::NoError) {
                    sourceFailed();
                }
            });
        retryTimer.stop();
        setReady(true);
        return true;
    }

    void scheduleRetry() {
        if (started && source == NULL && !retryTimer.isActive()) {
            retryTimer.start();
        }
    }

    void setReady(bool value) {
        if (ready == value) {
            return;
        }
        ready = value;
        const LocationMonitor::HealthCallback callback = healthCallback;
        QTimer::singleShot(0, q, [callback, value]() {
            callback(value);
        });
    }

    void destroySource() {
        ++sourceGeneration;
        if (source != NULL) {
            source->stopUpdates();
            source->disconnect(q);
            source->deleteLater();
            source = NULL;
        }
        updatesActive = false;
    }

    void stopUpdates() {
        if (source != NULL && updatesActive) {
            source->stopUpdates();
        }
        updatesActive = false;
    }

    void sourceFailed() {
        const QList<quint64> pending = requests.keys();
        requests.clear();
        deadlineTimer.stop();
        destroySource();
        setReady(false);
        scheduleRetry();
        for (QList<quint64>::const_iterator it = pending.constBegin();
             it != pending.constEnd(); ++it) {
            complete(*it, LP3_PLATFORM_UNAVAILABLE,
                     LocationMonitor::Fix());
        }
    }

    QGeoPositionInfoSource::PositioningMethods preferredMethods() const {
        bool highAccuracy = false;
        for (QMap<quint64, Request>::const_iterator it = requests.constBegin();
             it != requests.constEnd(); ++it) {
            if (it.value().highAccuracy) {
                highAccuracy = true;
                break;
            }
        }
        if (highAccuracy) {
            return QGeoPositionInfoSource::AllPositioningMethods;
        }
        const QGeoPositionInfoSource::PositioningMethods supported =
            source->supportedPositioningMethods();
        return supported.testFlag(
                   QGeoPositionInfoSource::NonSatellitePositioningMethods) ?
            QGeoPositionInfoSource::NonSatellitePositioningMethods :
            QGeoPositionInfoSource::AllPositioningMethods;
    }

    void updatePreferredMethods() {
        if (source != NULL && !requests.isEmpty()) {
            source->setPreferredPositioningMethods(preferredMethods());
        }
    }

    bool decodePosition(const QGeoPositionInfo &position,
                        LocationMonitor::Fix *fix) const {
        if (fix == NULL || !position.isValid() ||
            !position.timestamp().isValid() ||
            !position.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)) {
            return false;
        }
        const QGeoCoordinate coordinate = position.coordinate();
        const double latitude = coordinate.latitude();
        const double longitude = coordinate.longitude();
        const double accuracy =
            position.attribute(QGeoPositionInfo::HorizontalAccuracy);
        const qint64 timestampMs = position.timestamp().toMSecsSinceEpoch();
        if (!coordinate.isValid() || !finiteValue(latitude) ||
            !finiteValue(longitude) ||
            latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
            longitude > 180.0 || !finiteValue(accuracy) || accuracy < 0.0 ||
            accuracy > std::numeric_limits<int32_t>::max() ||
            timestampMs <= 0) {
            return false;
        }
        const qint64 latitudeE7 = qRound64(latitude * 10000000.0);
        const qint64 longitudeE7 = qRound64(longitude * 10000000.0);
        const double roundedAccuracy = std::ceil(accuracy);
        if (latitudeE7 < -900000000LL || latitudeE7 > 900000000LL ||
            longitudeE7 < -1800000000LL || longitudeE7 > 1800000000LL ||
            roundedAccuracy > std::numeric_limits<int32_t>::max()) {
            return false;
        }
        fix->latitudeE7 = static_cast<int32_t>(latitudeE7);
        fix->longitudeE7 = static_cast<int32_t>(longitudeE7);
        fix->accuracyM = static_cast<int32_t>(roundedAccuracy);
        fix->timestampMs = timestampMs;
        return true;
    }

    void acceptPosition(const QGeoPositionInfo &position) {
        // A position signal and the deadline timer can already be queued in
        // either order.  Retire elapsed requests before accepting the fix so
        // event-loop scheduling cannot extend the caller's timeout.
        expireRequests();
        LocationMonitor::Fix fix;
        if (requests.isEmpty() || !decodePosition(position, &fix)) {
            return;
        }
        const QList<quint64> pending = requests.keys();
        requests.clear();
        deadlineTimer.stop();
        stopUpdates();
        for (QList<quint64>::const_iterator it = pending.constBegin();
             it != pending.constEnd(); ++it) {
            complete(*it, LP3_PLATFORM_OK, fix);
        }
    }

    void expireRequests() {
        if (requests.isEmpty()) {
            deadlineTimer.stop();
            return;
        }
        const qint64 now = elapsed.elapsed();
        QList<quint64> expired;
        for (QMap<quint64, Request>::const_iterator it = requests.constBegin();
             it != requests.constEnd(); ++it) {
            if (it.value().deadlineMs <= now) {
                expired.append(it.key());
            }
        }
        for (QList<quint64>::const_iterator it = expired.constBegin();
             it != expired.constEnd(); ++it) {
            requests.remove(*it);
            complete(*it, LP3_PLATFORM_UNAVAILABLE,
                     LocationMonitor::Fix());
        }
        if (requests.isEmpty()) {
            deadlineTimer.stop();
            stopUpdates();
            destroySource();
            if (!ensureSource()) {
                setReady(false);
            }
        } else {
            updatePreferredMethods();
            scheduleDeadline();
        }
    }

    void scheduleDeadline() {
        if (requests.isEmpty()) {
            deadlineTimer.stop();
            return;
        }
        qint64 earliest = std::numeric_limits<qint64>::max();
        for (QMap<quint64, Request>::const_iterator it = requests.constBegin();
             it != requests.constEnd(); ++it) {
            if (it.value().deadlineMs < earliest) {
                earliest = it.value().deadlineMs;
            }
        }
        const qint64 remaining = earliest - elapsed.elapsed();
        deadlineTimer.start(static_cast<int>(remaining > 0 ? remaining : 0));
    }

    void complete(quint64 requestId, int32_t status,
                  const LocationMonitor::Fix &fix) {
        const LocationMonitor::CompletedCallback callback = completedCallback;
        QTimer::singleShot(0, q, [callback, requestId, status, fix]() {
            callback(requestId, status, fix);
        });
    }

    LocationMonitor *q;
    LocationMonitor::CompletedCallback completedCallback;
    LocationMonitor::HealthCallback healthCallback;
    SourceFactory sourceFactory;
    QGeoPositionInfoSource *source;
    bool started;
    bool ready;
    bool updatesActive;
    quint64 sourceGeneration;
    QElapsedTimer elapsed;
    QTimer retryTimer;
    QTimer deadlineTimer;
    QMap<quint64, Request> requests;
};

LocationMonitor::LocationMonitor(const CompletedCallback &completed,
                                 const HealthCallback &health,
                                 QObject *parent)
    : QObject(parent),
      m_private(new LocationMonitorPrivate(
          this, completed, health, createDefaultSource,
          kDefaultRetryIntervalMs)) {}

LocationMonitor::~LocationMonitor() {
    delete m_private;
    m_private = NULL;
}

bool LocationMonitor::start() {
    return m_private->start();
}

void LocationMonitor::query(quint64 requestId, bool highAccuracy,
                            uint32_t timeoutMs) {
    m_private->query(requestId, highAccuracy, timeoutMs);
}

bool LocationMonitor::cancel(quint64 requestId) {
    return m_private->cancel(requestId);
}

#ifdef LP3_LOCATION_TEST
LocationMonitor::LocationMonitor(const CompletedCallback &completed,
                                 const HealthCallback &health,
                                 const SourceFactory &factory,
                                 int retryIntervalMs,
                                 QObject *parent)
    : QObject(parent),
      m_private(new LocationMonitorPrivate(
          this, completed, health, factory, retryIntervalMs)) {}

int LocationMonitor::pendingForTest() const {
    return m_private->pendingForTest();
}

bool LocationMonitor::updatesActiveForTest() const {
    return m_private->updatesActiveForTest();
}
#endif
