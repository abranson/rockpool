/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServer>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <functional>

#include "libpebble3d-platform.h"
#include "mainvolumemonitor.h"

namespace {

const char kLookupService[] = "org.PulseAudio1";
const char kLookupInterface[] = "org.PulseAudio.ServerLookup1";
const char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";
const char kTestInterface[] = "org.rockpool.TestMainVolumeControl";

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

QString peerSocketPath() {
    const QByteArray testPath = qgetenv("ROCKPOOL_TEST_PULSE_SOCKET");
    if (!testPath.isEmpty()) {
        return QString::fromLocal8Bit(testPath);
    }
    return QStringLiteral("/run/user/%1/pulse/dbus-socket").arg(
        static_cast<qulonglong>(getuid()));
}

QString peerAddress() {
    return QStringLiteral("unix:path=") + peerSocketPath();
}

class FakeMainVolume : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.Meego.MainVolume2")
    Q_PROPERTY(quint32 StepCount READ stepCount)
    Q_PROPERTY(quint32 CurrentStep READ currentStep WRITE setCurrentStep)

public:
    FakeMainVolume() : m_stepCount(12), m_currentStep(3) {}

    quint32 stepCount() const { return m_stepCount; }
    quint32 currentStep() const { return m_currentStep; }
    void setCurrentStep(quint32 value) {
        if (value < m_stepCount && value != m_currentStep) {
            m_currentStep = value;
            emit StepsUpdated(m_stepCount, m_currentStep);
        }
    }

Q_SIGNALS:
    void StepsUpdated(quint32 stepCount, quint32 currentStep);

private:
    quint32 m_stepCount;
    quint32 m_currentStep;
};

class FakeLookup : public QDBusVirtualObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockpool.TestMainVolumeControl")

public:
    explicit FakeLookup(FakeMainVolume *volume)
        : m_volume(volume), m_hold(false), m_pendingConnection(QString()) {
        assert(m_volume != NULL);
    }

    QString introspect(const QString &) const override {
        return QStringLiteral(
            "<interface name=\"org.PulseAudio.ServerLookup1\">"
            "<property name=\"Address\" type=\"s\" access=\"read\"/>"
            "</interface>");
    }

    bool handleMessage(const QDBusMessage &message,
                       const QDBusConnection &connection) override {
        const QVariantList arguments = message.arguments();
        if (message.type() != QDBusMessage::MethodCallMessage ||
            message.interface() != QString::fromLatin1(kPropertiesInterface) ||
            message.member() != QStringLiteral("Get") || arguments.size() != 2 ||
            arguments.at(0).toString() != QString::fromLatin1(kLookupInterface) ||
            arguments.at(1).toString() != QStringLiteral("Address")) {
            connection.send(message.createErrorReply(
                QDBusError::InvalidArgs, QStringLiteral("invalid")));
            return true;
        }
        if (m_hold) {
            m_pendingConnection = connection;
            m_pending.append(message);
            return true;
        }
        connection.send(message.createReply(
            QVariant::fromValue(QDBusVariant(peerAddress()))));
        return true;
    }

public Q_SLOTS:
    void setHold(bool hold) { m_hold = hold; }
    int pending() const { return m_pending.size(); }
    void setRemoteCurrentStep(quint32 value) {
        m_volume->setCurrentStep(value);
    }
    void shutdown() {
        QTimer::singleShot(0, QCoreApplication::instance(), []() {
            QCoreApplication::quit();
        });
    }
    void dropOwnerAndRelease() {
        QDBusConnection::sessionBus().unregisterService(
            QString::fromLatin1(kLookupService));
        while (!m_pending.isEmpty()) {
            m_pendingConnection.send(m_pending.takeFirst().createReply(
                QVariant::fromValue(QDBusVariant(peerAddress()))));
        }
        QTimer::singleShot(0, QCoreApplication::instance(), []() {
            QCoreApplication::quit();
        });
    }

private:
    FakeMainVolume *m_volume;
    bool m_hold;
    QDBusConnection m_pendingConnection;
    QList<QDBusMessage> m_pending;
};

class Fixture : public QObject {
    Q_OBJECT

public:
    Fixture()
        : m_peerSocket(m_peerDirectory.path() + QStringLiteral("/dbus-socket")),
          m_server(new QDBusServer(QStringLiteral("unix:path=") + m_peerSocket,
                                   this)),
          m_lookup(&m_volume) {
        assert(m_peerDirectory.isValid());
        assert(m_server->isConnected());
        const QByteArray target = m_peerSocket.toLocal8Bit();
        const QByteArray link = peerSocketPath().toLocal8Bit();
        assert(symlink(target.constData(), link.constData()) == 0);
        QObject::connect(m_server, &QDBusServer::newConnection, this,
                         [this](const QDBusConnection &connection) {
            m_peers.append(connection);
            QDBusConnection peer = connection;
            assert(peer.registerObject(
                QStringLiteral("/com/meego/mainvolume2"), &m_volume,
                QDBusConnection::ExportAllProperties |
                QDBusConnection::ExportAllSignals));
        });
        QDBusConnection bus = QDBusConnection::sessionBus();
        assert(bus.registerService(QString::fromLatin1(kLookupService)));
        assert(bus.registerVirtualObject(
            QStringLiteral("/org/pulseaudio/server_lookup1"), &m_lookup));
        assert(bus.registerObject(QStringLiteral("/test"), &m_lookup,
                                  QDBusConnection::ExportAllSlots));
    }

    ~Fixture() {
        delete m_server;
        m_server = NULL;
        const QFileInfo link(peerSocketPath());
        if (link.isSymLink() && link.symLinkTarget() == m_peerSocket) {
            assert(QFile::remove(peerSocketPath()));
        }
    }

private:
    QTemporaryDir m_peerDirectory;
    QString m_peerSocket;
    QDBusServer *m_server;
    FakeMainVolume m_volume;
    FakeLookup m_lookup;
    QList<QDBusConnection> m_peers;
};

class PrivateSessionBus {
public:
    PrivateSessionBus() : m_pid(-1) {
        assert(m_directory.isValid());
        m_address = QByteArray("unix:path=") +
            m_directory.path().toLocal8Bit() + QByteArray("/bus");
        qputenv("DBUS_SESSION_BUS_ADDRESS", m_address);
        qputenv(
            "ROCKPOOL_TEST_PULSE_SOCKET",
            m_directory.path().toLocal8Bit() + QByteArray("/pulse-dbus-socket"));
        qunsetenv("DBUS_SESSION_BUS_PID");
        QProcess daemon;
        daemon.start(QStringLiteral("dbus-daemon"),
                     QStringList() << QStringLiteral("--session")
                         << QStringLiteral("--address=") +
                            QString::fromLocal8Bit(m_address)
                         << QStringLiteral("--fork")
                         << QStringLiteral("--print-pid"));
        assert(daemon.waitForFinished());
        bool valid = false;
        m_pid = QString::fromLocal8Bit(daemon.readAllStandardOutput()).trimmed().
            toLongLong(&valid);
        assert(valid && m_pid > 0);
    }

    ~PrivateSessionBus() {
        if (m_pid > 0) {
            assert(kill(static_cast<pid_t>(m_pid), SIGTERM) == 0);
            assert(waitFor([this]() {
                return kill(static_cast<pid_t>(m_pid), 0) == -1 && errno == ESRCH;
            }));
        }
    }

private:
    QTemporaryDir m_directory;
    QByteArray m_address;
    qint64 m_pid;
};

QDBusMessage control(
    const QString &method, const QVariantList &arguments = QVariantList(),
    const QString &service = QString::fromLatin1(kLookupService)) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        service, QStringLiteral("/test"), QString::fromLatin1(kTestInterface),
        method);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().call(message);
}

QString fixtureOwner() {
    const QDBusReply<QString> owner =
        QDBusConnection::sessionBus().interface()->serviceOwner(
            QString::fromLatin1(kLookupService));
    assert(owner.isValid() && !owner.value().isEmpty());
    return owner.value();
}

void stopFixture(QProcess *process, const QString &service) {
    const QDBusMessage reply = control(QStringLiteral("shutdown"),
                                       QVariantList(), service);
    assert(reply.type() == QDBusMessage::ReplyMessage);
    assert(process->waitForFinished());
    assert(!QFileInfo(peerSocketPath()).isSymLink());
}

void startFixture(QProcess *process, const QString &program) {
    process->start(program, QStringList() << QStringLiteral("--fixture"));
    assert(process->waitForStarted());
    assert(waitFor([]() {
        return QDBusConnection::sessionBus().interface()->isServiceRegistered(
            QString::fromLatin1(kLookupService));
    }));
}

void testMonitor(const QString &program) {
    QProcess fixture;
    startFixture(&fixture, program);

    QVector<MainVolumeMonitor::State> states;
    QVector<bool> health;
    QVector<char> order;
    MainVolumeMonitor monitor(
        [&states, &order](const MainVolumeMonitor::State &state) {
            states.append(state);
            order.append('s');
        },
        [&health, &order](bool ready) {
            health.append(ready);
            order.append('h');
        });
    assert(!monitor.start());
    assert(waitFor([&states, &health]() {
        return states.size() == 1 && health.size() == 1;
    }));
    assert(health[0] && order[0] == 'h' && order[1] == 's');
    assert(states[0].stepCount == 12 && states[0].currentStep == 3);

    assert(monitor.command(LP3_PLATFORM_MEDIA_PLAY_PAUSE) ==
           LP3_PLATFORM_INVALID_ARGUMENT);
    assert(monitor.command(LP3_PLATFORM_MEDIA_VOLUME_UP) == LP3_PLATFORM_OK);
    assert(waitFor([&states]() {
        return states.size() >= 2 && states.last().currentStep == 4;
    }));
    assert(monitor.command(LP3_PLATFORM_MEDIA_VOLUME_DOWN) == LP3_PLATFORM_OK);
    assert(waitFor([&states]() { return states.last().currentStep == 3; }));

    // Exercise the independent MainVolume2 signal path rather than relying on
    // command(), which also applies its successful write locally.
    const int statesBeforeSignal = states.size();
    const int healthBeforeSignal = health.size();
    QDBusMessage reply = control(QStringLiteral("setRemoteCurrentStep"),
                                 QVariantList() << quint32(7));
    assert(reply.type() == QDBusMessage::ReplyMessage);
    assert(waitFor([&states]() { return states.last().currentStep == 7; }));
    assert(states.size() > statesBeforeSignal);
    assert(health.size() == healthBeforeSignal);

    const quint64 presenceSession = monitor.sessionGenerationForTest();
    const quint64 presenceOwner = monitor.ownerGenerationForTest();
    assert(monitor.servicePresentForTest());

    stopFixture(&fixture, fixtureOwner());
    assert(waitFor([&health]() { return health.size() == 2; }));
    assert(!health[1]);
    assert(monitor.sessionGenerationForTest() == presenceSession);
    assert(monitor.ownerGenerationForTest() != presenceOwner);

    // A NameHasOwner(true) snapshot captured before owner loss must not
    // restore service presence or start a new lookup afterward.
    assert(!monitor.applyServicePresenceSnapshotForTest(
        presenceSession, presenceOwner, true));
    assert(!monitor.servicePresentForTest());

    // Hold Get(Address), drop the well-known owner, then release the stale
    // successful reply.  It must not reconnect or make Media ready.
    startFixture(&fixture, program);
    reply = control(QStringLiteral("setHold"), QVariantList() << true);
    assert(reply.type() == QDBusMessage::ReplyMessage);
    assert(waitFor([]() {
        const QDBusMessage pending = control(QStringLiteral("pending"));
        return pending.type() == QDBusMessage::ReplyMessage &&
            pending.arguments().first().toInt() > 0;
    }));
    const quint64 lookupReplies = monitor.lookupRepliesHandledForTest();
    reply = control(QStringLiteral("dropOwnerAndRelease"));
    assert(reply.type() == QDBusMessage::ReplyMessage);
    assert(waitFor([&monitor, lookupReplies]() {
        return monitor.lookupRepliesHandledForTest() > lookupReplies;
    }));
    assert(!monitor.peerActiveForTest());
    assert(health.size() == 2);
    assert(fixture.waitForFinished());
    assert(!QFileInfo(peerSocketPath()).isSymLink());
}

} // namespace

int main(int argc, char **argv) {
    const bool fixture = argc == 2 &&
        QString::fromLocal8Bit(argv[1]) == QStringLiteral("--fixture");
    if (fixture) {
        QCoreApplication application(argc, argv);
        Fixture service;
        return application.exec();
    }

    PrivateSessionBus bus;
    QCoreApplication application(argc, argv);
    testMonitor(application.applicationFilePath());
    return 0;
}

#include "mainvolumemonitor_test.moc"
