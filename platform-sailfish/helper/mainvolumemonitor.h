/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_MAIN_VOLUME_MONITOR_H
#define LIBPEBBLE3D_SAILFISH_MAIN_VOLUME_MONITOR_H

#include <QDBusAbstractInterface>
#include <QObject>

#include <stdint.h>

#include <functional>

class SailfishMainVolumeInterface : public QDBusAbstractInterface {
    Q_OBJECT

public:
    SailfishMainVolumeInterface(const QDBusConnection &connection,
                                QObject *parent = 0);

Q_SIGNALS:
    void StepsUpdated(quint32 stepCount, quint32 currentStep);
};

class MainVolumeMonitorPrivate;

class MainVolumeMonitor : public QObject {
public:
    struct State {
        uint32_t stepCount;
        uint32_t currentStep;

        State();
    };

    typedef std::function<void(const State &)> ChangedCallback;
    typedef std::function<void(bool)> HealthCallback;

    MainVolumeMonitor(const ChangedCallback &changed,
                      const HealthCallback &health,
                      QObject *parent = 0);
    ~MainVolumeMonitor();

    bool start();
    int32_t command(uint32_t command);

#ifdef LP3_MAINVOLUME_TEST
    quint64 sessionGenerationForTest() const;
    quint64 ownerGenerationForTest() const;
    bool applyServicePresenceSnapshotForTest(quint64 sessionGeneration,
                                             quint64 ownerGeneration,
                                             bool present);
    bool servicePresentForTest() const;
    bool peerActiveForTest() const;
    quint64 lookupRepliesHandledForTest() const;
#endif

private:
    MainVolumeMonitorPrivate *m_private;
};

#endif
