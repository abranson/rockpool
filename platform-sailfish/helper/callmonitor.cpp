/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "callmonitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>

#include "libpebble3d-platform.h"
#include "wire.h"

namespace {

const char kService[] = "org.nemomobile.voicecall";
const char kManagerPath[] = "/";
const char kManagerInterface[] =
    "org.nemomobile.voicecall.VoiceCallManager";
const char kCallInterface[] = "org.nemomobile.voicecall.VoiceCall";
const char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";
const char kDbusService[] = "org.freedesktop.DBus";
const char kDbusPath[] = "/org/freedesktop/DBus";
const char kDbusInterface[] = "org.freedesktop.DBus";
const char kCallPathPrefix[] = "/calls/";
const char kConnectionName[] = "libpebble3d-platform-calls";
const int kMaximumCalls = 8;
const int kCommandTimeoutMs = 1000;
const int kSnapshotTimeoutMs = 5000;

bool validId(const QString &id) {
    if (id.isEmpty() || id.size() > 128) {
        return false;
    }
    for (QString::const_iterator it = id.constBegin(); it != id.constEnd(); ++it) {
        const QChar character = *it;
        if (!((character >= QLatin1Char('A') &&
               character <= QLatin1Char('Z')) ||
              (character >= QLatin1Char('a') &&
               character <= QLatin1Char('z')) ||
              (character >= QLatin1Char('0') &&
               character <= QLatin1Char('9')) ||
              character == QLatin1Char('_'))) {
            return false;
        }
    }
    return true;
}

bool callEqual(const CallMonitor::Call &left, const CallMonitor::Call &right) {
    return left.state == right.state && left.id == right.id &&
        left.name == right.name && left.number == right.number;
}

int callPriority(uint32_t state) {
    switch (state) {
    case lp3wire::CallRinging:
        return 0;
    case lp3wire::CallActive:
        return 1;
    case lp3wire::CallDialing:
        return 2;
    case lp3wire::CallHeld:
        return 3;
    default:
        return 4;
    }
}

bool mapStatus(int status, uint32_t *state) {
    if (state == NULL) {
        return false;
    }
    switch (status) {
    case 5:
    case 6:
        *state = lp3wire::CallRinging;
        return true;
    case 3:
    case 4:
        *state = lp3wire::CallDialing;
        return true;
    case 1:
        *state = lp3wire::CallActive;
        return true;
    case 2:
        *state = lp3wire::CallHeld;
        return true;
    case 0:
    case 7:
    case 8:
    case 9:
        *state = lp3wire::CallEnded;
        return true;
    default:
        return false;
    }
}

} // namespace

SailfishVoiceCallManagerInterface::SailfishVoiceCallManagerInterface(
    const QDBusConnection &connection, QObject *parent)
    : QDBusAbstractInterface(QString::fromLatin1(kService),
                             QString::fromLatin1(kManagerPath),
                             kManagerInterface, connection, parent) {}

SailfishVoiceCallInterface::SailfishVoiceCallInterface(
    const QString &path, const QDBusConnection &connection, QObject *parent)
    : QDBusAbstractInterface(QString::fromLatin1(kService), path,
                             kCallInterface, connection, parent) {}

class CallMonitorPrivate {
public:
    CallMonitorPrivate(CallMonitor *owner,
                       const CallMonitor::ChangedCallback &changed,
                       const CallMonitor::HealthCallback &health)
        : q(owner), bus(QString::fromLatin1(kConnectionName)), manager(NULL),
          serviceWatcher(NULL), changedCallback(changed), healthCallback(health),
          started(false), ready(false), servicePresent(false), generation(0),
          connectionGeneration(0), snapshotInFlight(false) {
        retryTimer.setSingleShot(true);
        retryTimer.setInterval(1000);
        QObject::connect(&retryTimer, &QTimer::timeout, q, [this]() {
            if (started && servicePresent) {
                enumerate();
            }
        });
        connectionTimer.setInterval(1000);
        QObject::connect(&connectionTimer, &QTimer::timeout, q, [this]() {
            if (!started) {
                return;
            }
            if (!bus.isConnected()) {
                resetConnection();
                connectBus();
            }
        });
    }

    ~CallMonitorPrivate() {
        started = false;
        retryTimer.stop();
        connectionTimer.stop();
        QDBusConnection::disconnectFromBus(QString::fromLatin1(kConnectionName));
    }

    bool start() {
        if (started) {
            return ready;
        }
        started = true;
        connectionTimer.start();
        connectBus();
        return ready;
    }

    int32_t command(uint32_t commandValue, const QString &id) {
        QString path;
        QString method;
        if (commandValue == LP3_PLATFORM_CALL_ANSWER) {
            if (!validId(id)) {
                return LP3_PLATFORM_INVALID_ARGUMENT;
            }
            if (id != selected.id || selected.state != lp3wire::CallRinging) {
                return LP3_PLATFORM_UNAVAILABLE;
            }
            path = QString::fromLatin1(kCallPathPrefix) + id;
            method = QStringLiteral("answer");
        } else if (commandValue == LP3_PLATFORM_CALL_HANG_UP) {
            if (!validId(id)) {
                return LP3_PLATFORM_INVALID_ARGUMENT;
            }
            if (id != selected.id) {
                return LP3_PLATFORM_UNAVAILABLE;
            }
            path = QString::fromLatin1(kCallPathPrefix) + id;
            method = QStringLiteral("hangup");
        } else if (commandValue == LP3_PLATFORM_CALL_SILENCE) {
            if (!id.isEmpty()) {
                return LP3_PLATFORM_INVALID_ARGUMENT;
            }
            if (selected.state != lp3wire::CallRinging) {
                return LP3_PLATFORM_UNAVAILABLE;
            }
            path = QString::fromLatin1(kManagerPath);
            method = QStringLiteral("silenceRingtone");
        } else {
            return LP3_PLATFORM_INVALID_ARGUMENT;
        }
        if (!started || !ready || snapshotInFlight || !bus.isConnected() ||
            selected.id.isEmpty()) {
            return LP3_PLATFORM_UNAVAILABLE;
        }

        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kService), path,
            commandValue == LP3_PLATFORM_CALL_SILENCE ?
                QString::fromLatin1(kManagerInterface) :
                QString::fromLatin1(kCallInterface),
            method);
        const QDBusMessage reply = bus.call(
            message, QDBus::Block, kCommandTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            if (!bus.isConnected()) {
                resetConnection();
            }
            return LP3_PLATFORM_IO_ERROR;
        }
        if (commandValue == LP3_PLATFORM_CALL_SILENCE) {
            return reply.arguments().isEmpty() ? LP3_PLATFORM_OK :
                LP3_PLATFORM_IO_ERROR;
        }
        if (reply.arguments().size() != 1 ||
            reply.arguments().first().userType() != QMetaType::Bool ||
            !reply.arguments().first().toBool()) {
            return LP3_PLATFORM_IO_ERROR;
        }
        return LP3_PLATFORM_OK;
    }

private:
    void connectBus() {
        if (!started || bus.isConnected()) {
            return;
        }
        QDBusConnection::disconnectFromBus(QString::fromLatin1(kConnectionName));
        bus = QDBusConnection::connectToBus(
            QDBusConnection::SessionBus, QString::fromLatin1(kConnectionName));
        if (!bus.isConnected()) {
            return;
        }
        const quint64 currentConnection = ++connectionGeneration;
        serviceWatcher = new QDBusServiceWatcher(
            QString::fromLatin1(kService), bus,
            QDBusServiceWatcher::WatchForOwnerChange, q);
        QObject::connect(
            serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, q,
            [this, currentConnection](const QString &, const QString &oldOwner,
                   const QString &newOwner) {
                if (currentConnection != connectionGeneration) {
                    return;
                }
                if (!oldOwner.isEmpty()) {
                    clearService();
                }
                if (!newOwner.isEmpty()) {
                    beginService();
                }
            });

        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kDbusService), QString::fromLatin1(kDbusPath),
            QString::fromLatin1(kDbusInterface), QStringLiteral("NameHasOwner"));
        message << QString::fromLatin1(kService);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            bus.asyncCall(message, kSnapshotTimeoutMs), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q,
            [this, currentConnection](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<bool> reply = *finished;
                if (currentConnection != connectionGeneration) {
                    finished->deleteLater();
                    return;
                }
                if (reply.isError()) {
                    resetConnection();
                } else if (reply.value() && !servicePresent) {
                    beginService();
                }
                finished->deleteLater();
            });
    }

    void resetConnection() {
        ++connectionGeneration;
        clearService();
        if (serviceWatcher != NULL) {
            serviceWatcher->deleteLater();
            serviceWatcher = NULL;
        }
        QDBusConnection::disconnectFromBus(QString::fromLatin1(kConnectionName));
        bus = QDBusConnection(QString::fromLatin1(kConnectionName));
    }

    void setReady(bool value) {
        if (ready == value) {
            return;
        }
        ready = value;
        healthCallback(ready);
    }

    void emitEnded() {
        if (selected.id.isEmpty()) {
            return;
        }
        CallMonitor::Call ended;
        ended.state = lp3wire::CallEnded;
        ended.id = selected.id;
        selected = CallMonitor::Call();
        changedCallback(ended);
    }

    void clearInterfaces() {
        for (QMap<QString, SailfishVoiceCallInterface *>::iterator it =
                 interfaces.begin(); it != interfaces.end(); ++it) {
            it.value()->deleteLater();
        }
        interfaces.clear();
    }

    void clearService() {
        ++generation;
        retryTimer.stop();
        servicePresent = false;
        pendingIds.clear();
        pendingCalls.clear();
        snapshotInFlight = false;
        clearInterfaces();
        if (manager != NULL) {
            manager->deleteLater();
            manager = NULL;
        }
        emitEnded();
        setReady(false);
    }

    void beginService() {
        if (servicePresent || !bus.isConnected()) {
            return;
        }
        servicePresent = true;
        manager = new SailfishVoiceCallManagerInterface(bus, q);
        QObject::connect(
            manager, &SailfishVoiceCallManagerInterface::voiceCallsChanged, q,
            [this]() { enumerate(); });
        enumerate();
    }

    void snapshotFailed(quint64 snapshotGeneration) {
        if (snapshotGeneration != generation) {
            return;
        }
        pendingIds.clear();
        pendingCalls.clear();
        snapshotInFlight = false;
        // Retire the selected ID before making the domain unavailable.  A
        // later successful snapshot must then re-emit its live state instead
        // of suppressing it as an internal duplicate after clients reset.
        emitEnded();
        setReady(false);
        if (servicePresent && !retryTimer.isActive()) {
            retryTimer.start();
        }
    }

    void enumerate() {
        if (!servicePresent || manager == NULL) {
            return;
        }
        retryTimer.stop();
        const quint64 snapshotGeneration = ++generation;
        pendingIds.clear();
        pendingCalls.clear();
        snapshotInFlight = true;

        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kService), QString::fromLatin1(kManagerPath),
            QString::fromLatin1(kPropertiesInterface), QStringLiteral("Get"));
        message << QString::fromLatin1(kManagerInterface)
                << QStringLiteral("voiceCalls");
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            bus.asyncCall(message, kSnapshotTimeoutMs), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q,
            [this, snapshotGeneration](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QDBusVariant> reply = *finished;
                if (snapshotGeneration != generation) {
                    finished->deleteLater();
                    return;
                }
                if (reply.isError()) {
                    snapshotFailed(snapshotGeneration);
                } else {
                    const QVariant value = reply.value().variant();
                    if (value.userType() != QMetaType::QStringList) {
                        snapshotFailed(snapshotGeneration);
                    } else {
                        receiveCallPaths(snapshotGeneration,
                                         value.toStringList());
                    }
                }
                finished->deleteLater();
            });
    }

    void receiveCallPaths(quint64 snapshotGeneration,
                          const QStringList &paths) {
        QStringList ids;
        const QString prefix = QString::fromLatin1(kCallPathPrefix);
        for (QStringList::const_iterator it = paths.constBegin();
             it != paths.constEnd(); ++it) {
            const QString id = it->startsWith(prefix) ?
                it->mid(prefix.size()) : *it;
            if (validId(id) && (*it == id || *it == prefix + id) &&
                !ids.contains(id)) {
                ids.append(id);
            }
        }
        std::sort(ids.begin(), ids.end());
        if (ids.size() > kMaximumCalls) {
            ids = ids.mid(0, kMaximumCalls);
        }

        QSet<QString> desired;
        for (QStringList::const_iterator it = ids.constBegin();
             it != ids.constEnd(); ++it) {
            desired.insert(*it);
        }
        const QList<QString> oldIds = interfaces.keys();
        for (QList<QString>::const_iterator it = oldIds.constBegin();
             it != oldIds.constEnd(); ++it) {
            if (!desired.contains(*it)) {
                interfaces.take(*it)->deleteLater();
            }
        }

        pendingIds = desired;
        if (pendingIds.isEmpty()) {
            completeSnapshot(snapshotGeneration);
            return;
        }
        for (QStringList::const_iterator it = ids.constBegin();
             it != ids.constEnd(); ++it) {
            requestCall(snapshotGeneration, *it);
        }
    }

    void requestCall(quint64 snapshotGeneration, const QString &id) {
        SailfishVoiceCallInterface *callInterface = interfaces.value(id, NULL);
        if (callInterface == NULL) {
            const QString path = QString::fromLatin1(kCallPathPrefix) + id;
            callInterface = new SailfishVoiceCallInterface(path, bus, q);
            interfaces.insert(id, callInterface);
            QObject::connect(
                callInterface, &SailfishVoiceCallInterface::statusChanged, q,
                [this, id, callInterface](int, const QString &) {
                    if (interfaces.value(id, NULL) == callInterface) {
                        enumerate();
                    }
                });
            QObject::connect(
                callInterface, &SailfishVoiceCallInterface::lineIdChanged, q,
                [this, id, callInterface](const QString &) {
                    if (interfaces.value(id, NULL) == callInterface) {
                        enumerate();
                    }
                });
        }

        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kService),
            QString::fromLatin1(kCallPathPrefix) + id,
            QString::fromLatin1(kCallInterface), QStringLiteral("getProperties"));
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            bus.asyncCall(message, kSnapshotTimeoutMs), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q,
            [this, snapshotGeneration, id](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QVariantMap> reply = *finished;
                if (snapshotGeneration != generation) {
                    finished->deleteLater();
                    return;
                }
                if (reply.isError() ||
                    !receiveCall(snapshotGeneration, id, reply.value())) {
                    snapshotFailed(snapshotGeneration);
                }
                finished->deleteLater();
            });
    }

    bool receiveCall(quint64 snapshotGeneration, const QString &id,
                     const QVariantMap &properties) {
        if (snapshotGeneration != generation || !pendingIds.contains(id)) {
            return true;
        }
        const QVariant handlerId = properties.value(QStringLiteral("handlerId"));
        const QVariant statusValue = properties.value(QStringLiteral("status"));
        const QVariant lineId = properties.value(QStringLiteral("lineId"));
        const QVariant incoming = properties.value(QStringLiteral("isIncoming"));
        uint32_t state = lp3wire::CallEnded;
        if (handlerId.userType() != QMetaType::QString ||
            handlerId.toString() != id || !validId(handlerId.toString()) ||
            statusValue.userType() != QMetaType::Int ||
            !mapStatus(statusValue.toInt(), &state) ||
            lineId.userType() != QMetaType::QString ||
            incoming.userType() != QMetaType::Bool) {
            return false;
        }

        if (state != lp3wire::CallEnded) {
            CallMonitor::Call call;
            call.state = state;
            call.id = id;
            call.number = lineId.toString();
            pendingCalls.insert(id, call);
        }
        pendingIds.remove(id);
        if (pendingIds.isEmpty()) {
            completeSnapshot(snapshotGeneration);
        }
        return true;
    }

    void completeSnapshot(quint64 snapshotGeneration) {
        if (snapshotGeneration != generation) {
            return;
        }
        CallMonitor::Call next;
        for (QMap<QString, CallMonitor::Call>::const_iterator it =
                 pendingCalls.constBegin(); it != pendingCalls.constEnd(); ++it) {
            if (next.id.isEmpty() ||
                callPriority(it.value().state) < callPriority(next.state) ||
                (callPriority(it.value().state) == callPriority(next.state) &&
                 it.key() < next.id)) {
                next = it.value();
            }
        }
        pendingCalls.clear();

        // A call event is valid only while Calls is ready. Queue the Health
        // transition first on initial discovery or recovery; SOCK_SEQPACKET
        // preserves that order through the proxy's ready-domain gate.
        setReady(true);

        if (selected.id != next.id) {
            emitEnded();
            selected = next;
            if (!selected.id.isEmpty()) {
                changedCallback(selected);
            }
        } else if (!selected.id.isEmpty() && !callEqual(selected, next)) {
            selected = next;
            changedCallback(selected);
        }
        snapshotInFlight = false;
    }

    CallMonitor *q;
    QDBusConnection bus;
    SailfishVoiceCallManagerInterface *manager;
    QDBusServiceWatcher *serviceWatcher;
    QMap<QString, SailfishVoiceCallInterface *> interfaces;
    CallMonitor::ChangedCallback changedCallback;
    CallMonitor::HealthCallback healthCallback;
    bool started;
    bool ready;
    bool servicePresent;
    quint64 generation;
    QSet<QString> pendingIds;
    QMap<QString, CallMonitor::Call> pendingCalls;
    CallMonitor::Call selected;
    QTimer retryTimer;
    QTimer connectionTimer;
    quint64 connectionGeneration;
    bool snapshotInFlight;
};

CallMonitor::Call::Call() : state(lp3wire::CallEnded) {}

CallMonitor::CallMonitor(const ChangedCallback &changed,
                         const HealthCallback &health,
                         QObject *parent)
    : QObject(parent), m_private(new CallMonitorPrivate(this, changed, health)) {}

CallMonitor::~CallMonitor() {
    delete m_private;
}

bool CallMonitor::start() {
    return m_private->start();
}

int32_t CallMonitor::command(uint32_t command, const QString &id) {
    return m_private->command(command, id);
}
