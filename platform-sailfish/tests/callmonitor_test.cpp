/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>
#include <QVector>

#include <assert.h>
#include <errno.h>
#include <signal.h>

#include <functional>

#include "callmonitor.h"
#include "libpebble3d-platform.h"
#include "wire.h"

namespace {

const char kService[] = "org.nemomobile.voicecall";
const char kManagerInterface[] = "org.nemomobile.voicecall.VoiceCallManager";
const char kCallInterface[] = "org.nemomobile.voicecall.VoiceCall";

class FakeCall : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.nemomobile.voicecall.VoiceCall")

public:
    FakeCall(const QString &id, int status, const QString &lineId,
             bool incoming, QObject *parent)
        : QObject(parent), m_id(id), m_status(status), m_lineId(lineId),
          m_incoming(incoming), m_answers(0), m_hangups(0),
          m_holdProperties(false), m_delayedConnection(QString()) {}

    QVariantMap properties() const {
        QVariantMap result;
        result.insert(QStringLiteral("handlerId"), m_id);
        result.insert(QStringLiteral("status"), m_status);
        result.insert(QStringLiteral("lineId"), m_lineId);
        result.insert(QStringLiteral("isIncoming"), m_incoming);
        return result;
    }
    void setStatus(int status) {
        m_status = status;
        emit statusChanged(status, m_id);
    }
    int answers() const { return m_answers; }
    int hangups() const { return m_hangups; }
    void setHoldProperties(bool hold) { m_holdProperties = hold; }
    int heldProperties() const { return m_delayedMessages.size(); }
    void releaseProperties() {
        while (!m_delayedMessages.isEmpty()) {
            m_delayedConnection.send(m_delayedMessages.takeFirst().createReply(
                QVariant::fromValue(properties())));
        }
    }

public Q_SLOTS:
    QVariantMap getProperties() {
        if (m_holdProperties) {
            setDelayedReply(true);
            m_delayedConnection = connection();
            m_delayedMessages.append(message());
            return QVariantMap();
        }
        return properties();
    }
    bool answer() { ++m_answers; return true; }
    bool hangup() { ++m_hangups; return true; }

Q_SIGNALS:
    void statusChanged(int status, const QString &handlerId);
    void lineIdChanged(const QString &lineId);

private:
    QString m_id;
    int m_status;
    QString m_lineId;
    bool m_incoming;
    int m_answers;
    int m_hangups;
    bool m_holdProperties;
    QDBusConnection m_delayedConnection;
    QList<QDBusMessage> m_delayedMessages;
};

class FakeVoiceCallService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.nemomobile.voicecall.VoiceCallManager")
    Q_PROPERTY(QStringList voiceCalls READ voiceCalls)

public:
    explicit FakeVoiceCallService(QObject *parent = 0)
        : QObject(parent), m_silences(0), m_holdProperties(false) {}

    bool registerService() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        return bus.registerService(QString::fromLatin1(kService)) &&
            bus.registerObject(QStringLiteral("/"), this,
                               QDBusConnection::ExportAllSlots |
                               QDBusConnection::ExportAllSignals |
                               QDBusConnection::ExportAllProperties) &&
            bus.registerObject(QStringLiteral("/test"), this,
                               QDBusConnection::ExportAllSlots);
    }
    QStringList voiceCalls() const {
        QStringList paths;
        for (QMap<QString, FakeCall *>::const_iterator it = m_calls.constBegin();
             it != m_calls.constEnd(); ++it) {
            paths.append(QStringLiteral("/calls/") + it.key());
        }
        return paths;
    }

public Q_SLOTS:
    void silenceRingtone() { ++m_silences; }
    void addCall(const QString &id, int status, const QString &lineId,
                 bool incoming) {
        removeCall(id);
        FakeCall *call = new FakeCall(id, status, lineId, incoming, this);
        call->setHoldProperties(m_holdProperties);
        m_calls.insert(id, call);
        QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/calls/") + id, call,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        emit voiceCallsChanged();
    }
    void removeCall(const QString &id) {
        FakeCall *call = m_calls.take(id);
        if (call != NULL) {
            QDBusConnection::sessionBus().unregisterObject(
                QStringLiteral("/calls/") + id);
            call->deleteLater();
            emit voiceCallsChanged();
        }
    }
    void setStatus(const QString &id, int status) {
        FakeCall *call = m_calls.value(id, NULL);
        if (call != NULL) {
            call->setStatus(status);
        }
    }
    void holdProperties(bool hold) {
        m_holdProperties = hold;
        for (QMap<QString, FakeCall *>::const_iterator it = m_calls.constBegin();
             it != m_calls.constEnd(); ++it) {
            it.value()->setHoldProperties(hold);
        }
    }
    int heldProperties() const {
        int held = 0;
        for (QMap<QString, FakeCall *>::const_iterator it = m_calls.constBegin();
             it != m_calls.constEnd(); ++it) {
            held += it.value()->heldProperties();
        }
        return held;
    }
    void releaseProperties() {
        for (QMap<QString, FakeCall *>::const_iterator it = m_calls.constBegin();
             it != m_calls.constEnd(); ++it) {
            it.value()->releaseProperties();
        }
    }
    int answers(const QString &id) const {
        FakeCall *call = m_calls.value(id, NULL);
        return call == NULL ? -1 : call->answers();
    }
    int hangups(const QString &id) const {
        FakeCall *call = m_calls.value(id, NULL);
        return call == NULL ? -1 : call->hangups();
    }
    int silences() const { return m_silences; }

Q_SIGNALS:
    void voiceCallsChanged();

private:
    QMap<QString, FakeCall *> m_calls;
    int m_silences;
    bool m_holdProperties;
};

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    return true;
}

QDBusMessage control(const QString &method, const QVariantList &arguments = QVariantList()) {
    static int connectionNumber = 0;
    const QString connectionName = QStringLiteral("callmonitor-test-control-%1").
        arg(++connectionNumber);
    QDBusConnection bus = QDBusConnection::connectToBus(
        QString::fromLocal8Bit(qgetenv("DBUS_SESSION_BUS_ADDRESS")),
        connectionName);
    QDBusInterface interface(QString::fromLatin1(kService), QStringLiteral("/test"),
                             QString::fromLatin1(kManagerInterface),
                             bus);
    const QDBusMessage reply = interface.callWithArgumentList(
        QDBus::Block, method, arguments);
    QDBusConnection::disconnectFromBus(connectionName);
    return reply;
}

void requireReply(const QDBusMessage &reply) {
    assert(reply.type() == QDBusMessage::ReplyMessage);
}

void setUpCall(const QString &id, int status, const QString &number) {
    QVariantList arguments;
    arguments << id << status << number << true;
    requireReply(control(QStringLiteral("addCall"), arguments));
}

void runFixture(bool liveCall) {
    FakeVoiceCallService service;
    assert(service.registerService());
    if (liveCall) {
        service.addCall(QStringLiteral("gamma"), 5, QStringLiteral("333"), true);
    }
    QCoreApplication::exec();
}

void startFixture(QProcess *fixture, const QString &program, bool liveCall = false,
                  bool waitForService = true) {
    QStringList arguments;
    arguments << QStringLiteral("--fixture");
    if (liveCall) {
        arguments << QStringLiteral("--fixture-live-call");
    }
    fixture->start(program, arguments);
    assert(fixture->waitForStarted());
    if (waitForService) {
        assert(waitFor([]() {
            return QDBusConnection::sessionBus().interface()->isServiceRegistered(
                QString::fromLatin1(kService));
        }));
    }
}

class PrivateSessionBus {
public:
    PrivateSessionBus() : m_address(), m_pid(-1) {
        assert(m_directory.isValid());
        m_address = QByteArray("unix:path=") +
            m_directory.path().toLocal8Bit() + QByteArray("/bus");
        qputenv("DBUS_SESSION_BUS_ADDRESS", m_address);
        qunsetenv("DBUS_SESSION_BUS_PID");
        start();
    }

    ~PrivateSessionBus() {
        stop();
    }

    void restart() {
        stop();
        start();
    }

private:
    void start() {
        QProcess daemon;
        QStringList arguments;
        arguments << QStringLiteral("--session")
                  << QStringLiteral("--address=") +
            QString::fromLocal8Bit(m_address)
                  << QStringLiteral("--fork") << QStringLiteral("--print-pid");
        daemon.start(QStringLiteral("dbus-daemon"), arguments);
        assert(daemon.waitForFinished());
        assert(daemon.exitStatus() == QProcess::NormalExit &&
               daemon.exitCode() == 0);
        bool validPid = false;
        m_pid = QString::fromLocal8Bit(daemon.readAllStandardOutput()).trimmed().
            toLongLong(&validPid);
        assert(validPid && m_pid > 0);
    }

    void stop() {
        if (m_pid <= 0) {
            return;
        }
        assert(kill(static_cast<pid_t>(m_pid), SIGTERM) == 0);
        assert(waitFor([this]() {
            return kill(static_cast<pid_t>(m_pid), 0) == -1 && errno == ESRCH;
        }));
        m_pid = -1;
    }

    QTemporaryDir m_directory;
    QByteArray m_address;
    qint64 m_pid;
};

int result(const QDBusMessage &reply) {
    requireReply(reply);
    assert(reply.arguments().size() == 1);
    return reply.arguments().first().toInt();
}

void testMonitor(const QString &program, PrivateSessionBus *bus) {
    assert(bus != NULL);
    QProcess fixture;
    startFixture(&fixture, program);

    setUpCall(QStringLiteral("alpha"), 5, QStringLiteral("111"));
    setUpCall(QStringLiteral("beta"), 1, QStringLiteral("222"));

    QVector<CallMonitor::Call> calls;
    QVector<bool> health;
    QVector<char> order;
    CallMonitor monitor(
        [&calls, &order](const CallMonitor::Call &call) {
            calls.append(call);
            order.append('c');
        },
        [&health, &order](bool ready) {
            health.append(ready);
            order.append('h');
        });
    assert(!monitor.start());
    assert(waitFor([&calls, &health]() { return calls.size() == 1 && health.size() == 1; }));
    assert(health[0]);
    assert(order[0] == 'h' && order[1] == 'c');
    assert(calls[0].id == QStringLiteral("alpha"));
    assert(calls[0].state == lp3wire::CallRinging);

    requireReply(control(QStringLiteral("setStatus"),
                         QVariantList() << QStringLiteral("alpha") << 1));
    assert(waitFor([&calls]() { return calls.size() == 2; }));
    assert(calls[1].id == QStringLiteral("alpha"));
    assert(calls[1].state == lp3wire::CallActive);
    requireReply(control(QStringLiteral("setStatus"),
                         QVariantList() << QStringLiteral("alpha") << 1));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    assert(calls.size() == 2);

    assert(monitor.command(LP3_PLATFORM_CALL_ANSWER, QStringLiteral("alpha")) ==
           LP3_PLATFORM_UNAVAILABLE);
    requireReply(control(QStringLiteral("setStatus"),
                         QVariantList() << QStringLiteral("alpha") << 5));
    assert(waitFor([&calls]() { return calls.size() == 3; }));
    assert(monitor.command(LP3_PLATFORM_CALL_ANSWER, QStringLiteral("alpha")) ==
           LP3_PLATFORM_OK);
    assert(monitor.command(LP3_PLATFORM_CALL_SILENCE, QString()) == LP3_PLATFORM_OK);
    assert(monitor.command(LP3_PLATFORM_CALL_HANG_UP, QStringLiteral("alpha")) ==
           LP3_PLATFORM_OK);
    assert(result(control(QStringLiteral("answers"), QVariantList() << QStringLiteral("alpha"))) == 1);
    assert(result(control(QStringLiteral("hangups"), QVariantList() << QStringLiteral("alpha"))) == 1);
    assert(result(control(QStringLiteral("silences"))) == 1);
    assert(monitor.command(LP3_PLATFORM_CALL_SILENCE, QStringLiteral("alpha")) ==
           LP3_PLATFORM_INVALID_ARGUMENT);

    requireReply(control(QStringLiteral("removeCall"), QVariantList() << QStringLiteral("alpha")));
    assert(waitFor([&calls]() { return calls.size() == 5; }));
    assert(calls[3].state == lp3wire::CallEnded);
    assert(calls[3].id == QStringLiteral("alpha"));
    assert(calls[4].id == QStringLiteral("beta"));
    assert(calls[4].state == lp3wire::CallActive);

    fixture.terminate();
    assert(fixture.waitForFinished());
    assert(waitFor([&calls, &health]() { return calls.size() == 6 && health.size() == 2; }));
    assert(calls[5].state == lp3wire::CallEnded);
    assert(calls[5].id == QStringLiteral("beta"));
    assert(!health[1]);

    startFixture(&fixture, program);
    assert(waitFor([&health]() { return health.size() == 3; }));
    assert(health[2]);
    setUpCall(QStringLiteral("gamma"), 5, QStringLiteral("333"));
    assert(waitFor([&calls]() { return calls.size() == 7; }));
    assert(calls[6].id == QStringLiteral("gamma"));
    assert(calls[6].state == lp3wire::CallRinging);

    bus->restart();
    assert(waitFor([&calls, &health]() {
        return calls.size() == 8 && health.size() == 4;
    }));
    assert(calls[7].state == lp3wire::CallEnded);
    assert(calls[7].id == QStringLiteral("gamma"));
    assert(!health[3]);
    assert(order[order.size() - 2] == 'c' && order[order.size() - 1] == 'h');

    fixture.terminate();
    assert(fixture.waitForFinished());
    startFixture(&fixture, program, true, false);
    assert(waitFor([&calls, &health]() {
        return calls.size() == 9 && health.size() == 5;
    }));
    assert(health[4]);
    assert(calls[8].id == QStringLiteral("gamma"));
    assert(calls[8].state == lp3wire::CallRinging);
    assert(order[order.size() - 2] == 'h' && order[order.size() - 1] == 'c');

    requireReply(control(QStringLiteral("holdProperties"), QVariantList() << true));
    requireReply(control(QStringLiteral("setStatus"),
                         QVariantList() << QStringLiteral("gamma") << 1));
    assert(waitFor([]() {
        return result(control(QStringLiteral("heldProperties"))) > 0;
    }));
    QElapsedTimer commandTimer;
    commandTimer.start();
    assert(monitor.command(LP3_PLATFORM_CALL_ANSWER, QStringLiteral("gamma")) ==
           LP3_PLATFORM_UNAVAILABLE);
    assert(monitor.command(LP3_PLATFORM_CALL_HANG_UP, QStringLiteral("gamma")) ==
           LP3_PLATFORM_UNAVAILABLE);
    assert(monitor.command(LP3_PLATFORM_CALL_SILENCE, QString()) ==
           LP3_PLATFORM_UNAVAILABLE);
    assert(commandTimer.elapsed() < 200);
    assert(result(control(QStringLiteral("answers"),
                          QVariantList() << QStringLiteral("gamma"))) == 0);
    assert(result(control(QStringLiteral("hangups"),
                          QVariantList() << QStringLiteral("gamma"))) == 0);
    assert(result(control(QStringLiteral("silences"))) == 0);
    requireReply(control(QStringLiteral("holdProperties"), QVariantList() << false));
    requireReply(control(QStringLiteral("releaseProperties")));
    assert(waitFor([&calls]() { return calls.size() == 10; }));
    assert(calls[9].id == QStringLiteral("gamma"));
    assert(calls[9].state == lp3wire::CallActive);

    fixture.terminate();
    assert(fixture.waitForFinished());
}

} // namespace

int main(int argc, char **argv) {
    bool fixture = false;
    bool liveCall = false;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        fixture = fixture || argument == QStringLiteral("--fixture");
        liveCall = liveCall || argument == QStringLiteral("--fixture-live-call");
    }
    if (fixture) {
        QCoreApplication application(argc, argv);
        runFixture(liveCall);
        return 0;
    }

    PrivateSessionBus bus;
    QCoreApplication application(argc, argv);
    testMonitor(application.applicationFilePath(), &bus);
    return 0;
}

#include "callmonitor_test.moc"
