/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_LOCATION_MONITOR_H
#define LIBPEBBLE3D_SAILFISH_LOCATION_MONITOR_H

#include <QObject>

#include <stdint.h>

#include <functional>

class QGeoPositionInfoSource;
class LocationMonitorPrivate;

class LocationMonitor : public QObject {
public:
    struct Fix {
        int32_t latitudeE7;
        int32_t longitudeE7;
        int32_t accuracyM;
        int64_t timestampMs;

        Fix();
    };

    typedef std::function<void(quint64, int32_t, const Fix &)>
        CompletedCallback;
    typedef std::function<void(bool)> HealthCallback;

    LocationMonitor(const CompletedCallback &completed,
                    const HealthCallback &health,
                    QObject *parent = 0);
    ~LocationMonitor();

    bool start();
    void query(quint64 requestId, bool highAccuracy, uint32_t timeoutMs);
    bool cancel(quint64 requestId);

#ifdef LP3_LOCATION_TEST
    typedef std::function<QGeoPositionInfoSource *(QObject *)> SourceFactory;

    LocationMonitor(const CompletedCallback &completed,
                    const HealthCallback &health,
                    const SourceFactory &factory,
                    int retryIntervalMs,
                    QObject *parent = 0);
    int pendingForTest() const;
    bool updatesActiveForTest() const;
#endif

private:
    LocationMonitorPrivate *m_private;
};

#endif
