/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QList>
#include <QThread>

#include <assert.h>
#include <cmath>
#include <functional>

#include "locationmonitor.h"
#include "libpebble3d-platform.h"

namespace {

struct Result {
    quint64 id;
    int32_t status;
    LocationMonitor::Fix fix;
};

class FakeSource : public QGeoPositionInfoSource {
public:
    using QGeoPositionInfoSource::error;

    explicit FakeSource(QObject *parent = 0)
        : QGeoPositionInfoSource(parent), supported(
              SatellitePositioningMethods | NonSatellitePositioningMethods),
          starts(0), stops(0), requests(0) {}

    PositioningMethods supportedPositioningMethods() const override {
        return supported;
    }
    QGeoPositionInfo lastKnownPosition(bool) const override {
        return QGeoPositionInfo();
    }
    int minimumUpdateInterval() const override { return 0; }
    Error error() const override { return NoError; }
    void startUpdates() override { ++starts; }
    void stopUpdates() override { ++stops; }
    void requestUpdate(int) override { ++requests; }

    PositioningMethods supported;
    int starts;
    int stops;
    int requests;
};

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 1000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    return true;
}

QGeoPositionInfo validPosition() {
    QGeoPositionInfo position(QGeoCoordinate(51.5074, -0.1278),
                              QDateTime::fromMSecsSinceEpoch(1785678901234LL));
    position.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 11.2);
    return position;
}

void testFactoryHealthAndRetry() {
    QList<Result> results;
    QList<bool> health;
    FakeSource *source = 0;
    int attempts = 0;
    LocationMonitor::SourceFactory factory = [&source, &attempts](QObject *parent) {
        ++attempts;
        if (attempts == 1) {
            return static_cast<QGeoPositionInfoSource *>(0);
        }
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 20);
    assert(!monitor.start());
    assert(waitFor([&health]() { return health.size() == 1; }));
    assert(health[0]);
    assert(attempts >= 2 && source != 0);
}

void testSharedFixAndMethods() {
    QList<Result> results;
    QList<bool> health;
    FakeSource *source = 0;
    LocationMonitor::SourceFactory factory = [&source](QObject *parent) {
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 20);
    assert(monitor.start());
    monitor.query(1, false, 200);
    monitor.query(2, true, 200);
    assert(source->preferredPositioningMethods() ==
           QGeoPositionInfoSource::AllPositioningMethods);
    emit source->positionUpdated(validPosition());
    assert(waitFor([&results]() { return results.size() == 2; }));
    assert(results[0].status == LP3_PLATFORM_OK);
    assert(results[0].fix.latitudeE7 == 515074000);
    assert(results[0].fix.longitudeE7 == -1278000);
    assert(results[0].fix.accuracyM == 12);
    assert(results[0].fix.timestampMs == 1785678901234LL);
    assert(source->stops > 0);
}

void testIndividualDeadlinesAndCancellation() {
    QList<Result> results;
    QList<bool> health;
    FakeSource *source = 0;
    LocationMonitor::SourceFactory factory = [&source](QObject *parent) {
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 20);
    assert(monitor.start());
    monitor.query(10, false, 25);
    monitor.query(11, false, 120);
    assert(waitFor([&results]() { return results.size() == 1; }));
    assert(results[0].id == 10 && results[0].status == LP3_PLATFORM_UNAVAILABLE);
    assert(monitor.cancel(11));
    assert(!monitor.cancel(11));
    emit source->positionUpdated(validPosition());
    QThread::msleep(20);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    assert(results.size() == 1);
}

void testDeadlineWinsOverLateFix() {
    QList<Result> results;
    QList<bool> health;
    FakeSource *source = 0;
    LocationMonitor::SourceFactory factory = [&source](QObject *parent) {
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 20);
    assert(monitor.start());
    monitor.query(12, false, 10);

    // Do not process the deadline timer.  Deliver a valid source signal after
    // the monotonic deadline and prove the signal cannot win by queue order.
    QThread::msleep(20);
    emit source->positionUpdated(validPosition());
    assert(waitFor([&results]() { return results.size() == 1; }));
    assert(results[0].id == 12);
    assert(results[0].status == LP3_PLATFORM_UNAVAILABLE);
}

void testMalformedFixes() {
    QList<Result> results;
    QList<bool> health;
    FakeSource *source = 0;
    LocationMonitor::SourceFactory factory = [&source](QObject *parent) {
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 20);
    assert(monitor.start());
    monitor.query(20, false, 150);
    QGeoPositionInfo bad = validPosition();
    bad.setCoordinate(QGeoCoordinate(91.0, 0.0));
    emit source->positionUpdated(bad);
    bad = validPosition();
    bad.setAttribute(QGeoPositionInfo::HorizontalAccuracy, -1.0);
    emit source->positionUpdated(bad);
    bad = validPosition();
    bad.setTimestamp(QDateTime());
    emit source->positionUpdated(bad);
    assert(waitFor([&results]() { return results.size() == 1; }));
    assert(results[0].status == LP3_PLATFORM_UNAVAILABLE);
}

void testSourceErrorAndGeneration() {
    QList<Result> results;
    QList<bool> health;
    QList<FakeSource *> sources;
    LocationMonitor::SourceFactory factory = [&sources](QObject *parent) {
        FakeSource *source = new FakeSource(parent);
        sources.append(source);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 10);
    assert(monitor.start());
    monitor.query(30, false, 200);
    FakeSource *old = sources[0];
    emit old->error(QGeoPositionInfoSource::ClosedError);
    emit old->positionUpdated(validPosition());
    assert(waitFor([&results]() { return results.size() == 1; }));
    assert(results[0].status == LP3_PLATFORM_UNAVAILABLE);
    assert(waitFor([&sources, &health]() {
        return sources.size() >= 2 && health.size() >= 3;
    }));
    assert(results.size() == 1);
    assert(health.size() >= 3 && !health[1] && health.last());
}

void testCallbackCanDestroyMonitor() {
    FakeSource *source = 0;
    int callbacks = 0;
    LocationMonitor *monitor = 0;
    LocationMonitor::SourceFactory factory = [&source](QObject *parent) {
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    monitor = new LocationMonitor(
        [&monitor, &callbacks](quint64, int32_t,
                               const LocationMonitor::Fix &) {
            ++callbacks;
            delete monitor;
            monitor = 0;
        },
        [](bool) {}, factory, 20);
    assert(monitor->start());
    monitor->query(31, false, 200);
    monitor->query(32, false, 200);
    emit source->positionUpdated(validPosition());

    assert(waitFor([&monitor]() { return monitor == 0; }));
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    assert(callbacks == 1);
}

void testCoarseMethodSelection() {
    QList<Result> results;
    QList<bool> health;
    FakeSource *source = 0;
    LocationMonitor::SourceFactory factory = [&source](QObject *parent) {
        source = new FakeSource(parent);
        return static_cast<QGeoPositionInfoSource *>(source);
    };
    LocationMonitor monitor(
        [&results](quint64 id, int32_t status, const LocationMonitor::Fix &fix) {
            Result result = { id, status, fix };
            results.append(result);
        },
        [&health](bool ready) { health.append(ready); }, factory, 20);
    assert(monitor.start());
    monitor.query(40, false, 100);
    assert(source->preferredPositioningMethods() ==
           QGeoPositionInfoSource::NonSatellitePositioningMethods);
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    testFactoryHealthAndRetry();
    testSharedFixAndMethods();
    testIndividualDeadlinesAndCancellation();
    testDeadlineWinsOverLateFix();
    testMalformedFixes();
    testSourceErrorAndGeneration();
    testCallbackCanDestroyMonitor();
    testCoarseMethodSelection();
    return 0;
}
