/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mainvolumemonitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QMetaType>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <unistd.h>

#include "libpebble3d-platform.h"

namespace {

const char kLookupService[] = "org.PulseAudio1";
const char kLookupPath[] = "/org/pulseaudio/server_lookup1";
const char kLookupInterface[] = "org.PulseAudio.ServerLookup1";
const char kMainVolumePath[] = "/com/meego/mainvolume2";
const char kMainVolumeInterface[] = "com.Meego.MainVolume2";
const char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";
const char kDbusService[] = "org.freedesktop.DBus";
const char kDbusPath[] = "/org/freedesktop/DBus";
const char kDbusInterface[] = "org.freedesktop.DBus";
const char kSessionConnectionName[] =
    "libpebble3d-platform-media-session";
const char kPeerConnectionName[] = "libpebble3d-platform-mainvolume";
const int kCommandTimeoutMs = 1000;
const int kSnapshotTimeoutMs = 5000;
const uint32_t kMaximumStepCount = 1000;

bool uintProperty(const QVariantMap &properties, const QString &name,
                  uint32_t *value) {
    const QVariant property = properties.value(name);
    if (value == NULL || property.userType() != QMetaType::UInt) {
        return false;
    }
    *value = property.toUInt();
    return true;
}

bool validState(uint32_t stepCount, uint32_t currentStep) {
    return stepCount >= 2 && stepCount <= kMaximumStepCount &&
        currentStep < stepCount;
}

} // namespace

SailfishMainVolumeInterface::SailfishMainVolumeInterface(
    const QDBusConnection &connection, QObject *parent)
    : QDBusAbstractInterface(QString(),
                             QString::fromLatin1(kMainVolumePath),
                             kMainVolumeInterface, connection, parent) {}

class MainVolumeMonitorPrivate {
public:
    MainVolumeMonitorPrivate(MainVolumeMonitor *owner,
                             const MainVolumeMonitor::ChangedCallback &changed,
                             const MainVolumeMonitor::HealthCallback &health)
        : q(owner),
          sessionBus(QString::fromLatin1(kSessionConnectionName)),
          peerBus(QString::fromLatin1(kPeerConnectionName)),
          serviceWatcher(NULL), mainVolume(NULL), changedCallback(changed),
          healthCallback(health), started(false), ready(false),
          servicePresent(false), lookupInFlight(false), snapshotInFlight(false),
          sessionGeneration(0), ownerGeneration(0), lookupGeneration(0),
          peerGeneration(0), lookupRepliesHandled(0), haveState(false) {
        connectionTimer.setInterval(1000);
        QObject::connect(&connectionTimer, &QTimer::timeout, q, [this]() {
            maintainConnections();
        });
    }

    ~MainVolumeMonitorPrivate() {
        started = false;
        connectionTimer.stop();
        QDBusConnection::disconnectFromPeer(
            QString::fromLatin1(kPeerConnectionName));
        QDBusConnection::disconnectFromBus(
            QString::fromLatin1(kSessionConnectionName));
    }

    bool start() {
        if (started) {
            return ready;
        }
        started = true;
        connectionTimer.start();
        connectSessionBus();
        return ready;
    }

    int32_t command(uint32_t commandValue) {
        if (commandValue != LP3_PLATFORM_MEDIA_VOLUME_UP &&
            commandValue != LP3_PLATFORM_MEDIA_VOLUME_DOWN) {
            return LP3_PLATFORM_INVALID_ARGUMENT;
        }
        if (!started || !ready || !haveState || snapshotInFlight ||
            !peerBus.isConnected()) {
            return LP3_PLATFORM_UNAVAILABLE;
        }

        uint32_t nextStep = state.currentStep;
        if (commandValue == LP3_PLATFORM_MEDIA_VOLUME_UP) {
            if (nextStep == state.stepCount - 1) {
                return LP3_PLATFORM_OK;
            }
            ++nextStep;
        } else {
            if (nextStep == 0) {
                return LP3_PLATFORM_OK;
            }
            --nextStep;
        }

        QDBusMessage message = QDBusMessage::createMethodCall(
            QString(),
            QString::fromLatin1(kMainVolumePath),
            QString::fromLatin1(kPropertiesInterface), QStringLiteral("Set"));
        message << QString::fromLatin1(kMainVolumeInterface)
                << QStringLiteral("CurrentStep")
                << QVariant::fromValue(QDBusVariant(
                       QVariant::fromValue(static_cast<quint32>(nextStep))));
        const QDBusMessage reply = peerBus.call(
            message, QDBus::Block, kCommandTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage ||
            !reply.arguments().isEmpty()) {
            resetPeer();
            return LP3_PLATFORM_IO_ERROR;
        }

        // A successful property write is authoritative.  Emit it immediately;
        // the matching StepsUpdated signal is harmlessly coalesced upstream.
        acceptState(state.stepCount, nextStep);
        return LP3_PLATFORM_OK;
    }

#ifdef LP3_MAINVOLUME_TEST
    quint64 sessionGenerationForTest() const { return sessionGeneration; }
    quint64 ownerGenerationForTest() const { return ownerGeneration; }
    bool applyServicePresenceSnapshotForTest(quint64 session,
                                             quint64 owner,
                                             bool present) {
        return applyServicePresenceSnapshot(session, owner, present);
    }
    bool servicePresentForTest() const { return servicePresent; }
    bool peerActiveForTest() const {
        return mainVolume != NULL || peerBus.isConnected();
    }
    quint64 lookupRepliesHandledForTest() const {
        return lookupRepliesHandled;
    }
#endif

private:
    QString expectedPeerAddress() const {
#ifdef LP3_MAINVOLUME_TEST
        const QByteArray testPath = qgetenv("ROCKPOOL_TEST_PULSE_SOCKET");
        if (!testPath.isEmpty()) {
            return QStringLiteral("unix:path=") + QString::fromLocal8Bit(testPath);
        }
#endif
        return QStringLiteral("unix:path=/run/user/%1/pulse/dbus-socket").arg(
            static_cast<qulonglong>(getuid()));
    }

    void maintainConnections() {
        if (!started) {
            return;
        }
        if (!sessionBus.isConnected()) {
            resetSessionBus();
            connectSessionBus();
            return;
        }
        if (mainVolume != NULL && !peerBus.isConnected()) {
            resetPeer();
        }
        if (servicePresent && mainVolume == NULL && !lookupInFlight) {
            lookupPeer();
        }
    }

    void connectSessionBus() {
        if (!started || sessionBus.isConnected()) {
            return;
        }
        QDBusConnection::disconnectFromBus(
            QString::fromLatin1(kSessionConnectionName));
        sessionBus = QDBusConnection::connectToBus(
            QDBusConnection::SessionBus,
            QString::fromLatin1(kSessionConnectionName));
        if (!sessionBus.isConnected()) {
            return;
        }

        const quint64 generation = ++sessionGeneration;
        serviceWatcher = new QDBusServiceWatcher(
            QString::fromLatin1(kLookupService), sessionBus,
            QDBusServiceWatcher::WatchForOwnerChange, q);
        QObject::connect(
            serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, q,
            [this, generation](const QString &, const QString &oldOwner,
                               const QString &newOwner) {
                if (generation != sessionGeneration) {
                    return;
                }
                ++ownerGeneration;
                if (!oldOwner.isEmpty()) {
                    servicePresent = false;
                    resetPeer();
                }
                if (!newOwner.isEmpty()) {
                    servicePresent = true;
                    lookupPeer();
                }
            });

        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kDbusService), QString::fromLatin1(kDbusPath),
            QString::fromLatin1(kDbusInterface), QStringLiteral("NameHasOwner"));
        message << QString::fromLatin1(kLookupService);
        const quint64 owner = ownerGeneration;
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            sessionBus.asyncCall(message, kSnapshotTimeoutMs), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q,
            [this, generation, owner](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<bool> reply = *finished;
                if (!servicePresenceSnapshotCurrent(generation, owner)) {
                    finished->deleteLater();
                    return;
                }
                if (reply.isError()) {
                    resetSessionBus();
                } else {
                    applyServicePresenceSnapshot(generation, owner,
                                                 reply.value());
                }
                finished->deleteLater();
            });
    }

    void resetSessionBus() {
        ++sessionGeneration;
        ++ownerGeneration;
        servicePresent = false;
        if (serviceWatcher != NULL) {
            serviceWatcher->deleteLater();
            serviceWatcher = NULL;
        }
        resetPeer();
        QDBusConnection::disconnectFromBus(
            QString::fromLatin1(kSessionConnectionName));
        sessionBus = QDBusConnection(
            QString::fromLatin1(kSessionConnectionName));
    }

    bool servicePresenceSnapshotCurrent(quint64 session,
                                        quint64 owner) const {
        return session == sessionGeneration && owner == ownerGeneration;
    }

    bool applyServicePresenceSnapshot(quint64 session, quint64 owner,
                                      bool present) {
        if (!servicePresenceSnapshotCurrent(session, owner)) {
            return false;
        }
        servicePresent = present;
        if (servicePresent) {
            lookupPeer();
        }
        return true;
    }

    void lookupPeer() {
        if (!started || !servicePresent || !sessionBus.isConnected() ||
            lookupInFlight || mainVolume != NULL) {
            return;
        }
        lookupInFlight = true;
        const quint64 session = sessionGeneration;
        const quint64 lookup = ++lookupGeneration;
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kLookupService),
            QString::fromLatin1(kLookupPath),
            QString::fromLatin1(kPropertiesInterface), QStringLiteral("Get"));
        message << QString::fromLatin1(kLookupInterface)
                << QStringLiteral("Address");
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            sessionBus.asyncCall(message, kSnapshotTimeoutMs), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q,
            [this, session, lookup](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QDBusVariant> reply = *finished;
                ++lookupRepliesHandled;
                if (session != sessionGeneration ||
                    lookup != lookupGeneration || !lookupInFlight ||
                    !servicePresent) {
                    finished->deleteLater();
                    return;
                }
                lookupInFlight = false;
                if (reply.isError() ||
                    reply.value().variant().userType() != QMetaType::QString ||
                    reply.value().variant().toString() != expectedPeerAddress()) {
                    setReady(false);
                } else {
                    connectPeer(reply.value().variant().toString());
                }
                finished->deleteLater();
            });
    }

    void connectPeer(const QString &address) {
        if (address != expectedPeerAddress()) {
            setReady(false);
            return;
        }
        QDBusConnection::disconnectFromPeer(
            QString::fromLatin1(kPeerConnectionName));
        peerBus = QDBusConnection::connectToPeer(
            address, QString::fromLatin1(kPeerConnectionName));
        if (!peerBus.isConnected()) {
            peerBus = QDBusConnection(QString::fromLatin1(kPeerConnectionName));
            setReady(false);
            return;
        }

        const quint64 generation = ++peerGeneration;
        mainVolume = new SailfishMainVolumeInterface(peerBus, q);
        QObject::connect(
            mainVolume, &SailfishMainVolumeInterface::StepsUpdated, q,
            [this, generation](quint32 stepCount, quint32 currentStep) {
                if (generation != peerGeneration) {
                    return;
                }
                snapshotInFlight = false;
                if (!validState(stepCount, currentStep)) {
                    resetPeer();
                    return;
                }
                acceptState(stepCount, currentStep);
            });
        snapshot();
    }

    void resetPeer() {
        ++peerGeneration;
        ++lookupGeneration;
        lookupInFlight = false;
        snapshotInFlight = false;
        haveState = false;
        if (mainVolume != NULL) {
            mainVolume->deleteLater();
            mainVolume = NULL;
        }
        QDBusConnection::disconnectFromPeer(
            QString::fromLatin1(kPeerConnectionName));
        peerBus = QDBusConnection(QString::fromLatin1(kPeerConnectionName));
        setReady(false);
    }

    void snapshot() {
        if (mainVolume == NULL || !peerBus.isConnected() || snapshotInFlight) {
            return;
        }
        snapshotInFlight = true;
        const quint64 generation = peerGeneration;
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString(),
            QString::fromLatin1(kMainVolumePath),
            QString::fromLatin1(kPropertiesInterface),
            QStringLiteral("GetAll"));
        message << QString::fromLatin1(kMainVolumeInterface);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            peerBus.asyncCall(message, kSnapshotTimeoutMs), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q,
            [this, generation](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QVariantMap> reply = *finished;
                if (generation != peerGeneration) {
                    finished->deleteLater();
                    return;
                }
                snapshotInFlight = false;
                uint32_t stepCount = 0;
                uint32_t currentStep = 0;
                if (reply.isError() ||
                    !uintProperty(reply.value(), QStringLiteral("StepCount"),
                                  &stepCount) ||
                    !uintProperty(reply.value(), QStringLiteral("CurrentStep"),
                                  &currentStep) ||
                    !validState(stepCount, currentStep)) {
                    resetPeer();
                } else {
                    acceptState(stepCount, currentStep);
                }
                finished->deleteLater();
            });
    }

    void acceptState(uint32_t stepCount, uint32_t currentStep) {
        MainVolumeMonitor::State next;
        next.stepCount = stepCount;
        next.currentStep = currentStep;
        const bool changed = !haveState || state.stepCount != next.stepCount ||
            state.currentStep != next.currentStep;
        state = next;
        haveState = true;

        // The ready Health frame must precede the event: the proxy rejects
        // domain events until their corresponding ready bit is established.
        setReady(true);
        if (changed) {
            changedCallback(state);
        }
    }

    void setReady(bool value) {
        if (ready == value) {
            return;
        }
        ready = value;
        healthCallback(ready);
    }

    MainVolumeMonitor *q;
    QDBusConnection sessionBus;
    QDBusConnection peerBus;
    QDBusServiceWatcher *serviceWatcher;
    SailfishMainVolumeInterface *mainVolume;
    MainVolumeMonitor::ChangedCallback changedCallback;
    MainVolumeMonitor::HealthCallback healthCallback;
    bool started;
    bool ready;
    bool servicePresent;
    bool lookupInFlight;
    bool snapshotInFlight;
    quint64 sessionGeneration;
    quint64 ownerGeneration;
    quint64 lookupGeneration;
    quint64 peerGeneration;
    quint64 lookupRepliesHandled;
    bool haveState;
    MainVolumeMonitor::State state;
    QTimer connectionTimer;
};

MainVolumeMonitor::State::State() : stepCount(0), currentStep(0) {}

MainVolumeMonitor::MainVolumeMonitor(const ChangedCallback &changed,
                                     const HealthCallback &health,
                                     QObject *parent)
    : QObject(parent),
      m_private(new MainVolumeMonitorPrivate(this, changed, health)) {}

MainVolumeMonitor::~MainVolumeMonitor() {
    delete m_private;
}

bool MainVolumeMonitor::start() {
    return m_private->start();
}

int32_t MainVolumeMonitor::command(uint32_t command) {
    return m_private->command(command);
}

#ifdef LP3_MAINVOLUME_TEST
quint64 MainVolumeMonitor::sessionGenerationForTest() const {
    return m_private->sessionGenerationForTest();
}

quint64 MainVolumeMonitor::ownerGenerationForTest() const {
    return m_private->ownerGenerationForTest();
}

bool MainVolumeMonitor::applyServicePresenceSnapshotForTest(
    quint64 sessionGeneration, quint64 ownerGeneration, bool present) {
    return m_private->applyServicePresenceSnapshotForTest(
        sessionGeneration, ownerGeneration, present);
}

bool MainVolumeMonitor::servicePresentForTest() const {
    return m_private->servicePresentForTest();
}

bool MainVolumeMonitor::peerActiveForTest() const {
    return m_private->peerActiveForTest();
}

quint64 MainVolumeMonitor::lookupRepliesHandledForTest() const {
    return m_private->lookupRepliesHandledForTest();
}
#endif
