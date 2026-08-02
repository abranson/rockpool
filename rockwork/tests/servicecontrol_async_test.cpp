/*
 * Regression coverage for asynchronous org.freedesktop.systemd1 requests.
 *
 * Run with run-pebbles-async-test.sh so the test owns the private session bus.
 */
#include "servicecontrol.h"

#include <QtTest>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariantMap>

namespace {
const char serviceName[] = "org.freedesktop.systemd1";
const char managerPath[] = "/org/freedesktop/systemd1";
const char unitPath[] = "/org/freedesktop/systemd1/unit/libpebble3d_2eservice";
const char unitInterface[] = "org.freedesktop.systemd1.Unit";
const char propertiesInterface[] = "org.freedesktop.DBus.Properties";

class DelayedSystemdManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.systemd1.Manager")

public:
    explicit DelayedSystemdManager(const QDBusConnection &connection)
        : m_connection(connection)
    {
    }

    void defer(const QString &method)
    {
        m_deferred << method;
    }

    int pendingCount(const QString &method) const
    {
        return m_pending.value(method).count();
    }

    QStringList calls() const
    {
        return m_calls;
    }

    void clearCalls()
    {
        m_calls.clear();
    }

    void replyNext(const QString &method)
    {
        QVERIFY2(!m_pending.value(method).isEmpty(), qPrintable(method));
        const QDBusMessage request = m_pending[method].takeFirst();
        QVERIFY(m_connection.send(request.createReply(replyArguments(method))));
    }

public slots:
    void Subscribe()
    {
        replyOrDelay(QStringLiteral("Subscribe"));
    }

    void LoadUnit(const QString &name)
    {
        QCOMPARE(name, ROCKPOOLD_SYSTEMD_UNIT);
        replyOrDelay(QStringLiteral("LoadUnit"));
    }

    void EnableUnitFiles(const QStringList &units, bool runtime, bool force)
    {
        QCOMPARE(units, QStringList() << ROCKPOOLD_SYSTEMD_UNIT);
        QCOMPARE(runtime, false);
        QCOMPARE(force, true);
        replyOrDelay(QStringLiteral("EnableUnitFiles"));
    }

    void DisableUnitFiles(const QStringList &units, bool runtime)
    {
        QCOMPARE(units, QStringList() << ROCKPOOLD_SYSTEMD_UNIT);
        QCOMPARE(runtime, false);
        replyOrDelay(QStringLiteral("DisableUnitFiles"));
    }

    void Reload()
    {
        replyOrDelay(QStringLiteral("Reload"));
    }

    void StartUnit(const QString &name, const QString &mode)
    {
        QCOMPARE(name, ROCKPOOLD_SYSTEMD_UNIT);
        QCOMPARE(mode, QStringLiteral("replace"));
        replyOrDelay(QStringLiteral("StartUnit"));
    }

    void StopUnit(const QString &name, const QString &mode)
    {
        QCOMPARE(name, ROCKPOOLD_SYSTEMD_UNIT);
        QCOMPARE(mode, QStringLiteral("replace"));
        replyOrDelay(QStringLiteral("StopUnit"));
    }

    void RestartUnit(const QString &name, const QString &mode)
    {
        QCOMPARE(name, ROCKPOOLD_SYSTEMD_UNIT);
        QCOMPARE(mode, QStringLiteral("replace"));
        replyOrDelay(QStringLiteral("RestartUnit"));
    }

private:
    QVariantList replyArguments(const QString &method) const
    {
        if (method == QStringLiteral("LoadUnit")) {
            return QVariantList() << QDBusObjectPath(QString::fromLatin1(unitPath));
        }
        if (method == QStringLiteral("StartUnit") ||
                method == QStringLiteral("StopUnit") ||
                method == QStringLiteral("RestartUnit")) {
            return QVariantList() << QDBusObjectPath(
                        QStringLiteral("/org/freedesktop/systemd1/job/test"));
        }
        return QVariantList();
    }

    void replyOrDelay(const QString &method)
    {
        setDelayedReply(true);
        m_calls << method;
        if (m_deferred.contains(method)) {
            m_pending[method].append(message());
            return;
        }
        QVERIFY(m_connection.send(message().createReply(replyArguments(method))));
    }

    QDBusConnection m_connection;
    QStringList m_deferred;
    QHash<QString, QList<QDBusMessage> > m_pending;
    QStringList m_calls;
};

class DelayedUnitProperties : public QDBusVirtualObject
{
public:
    explicit DelayedUnitProperties(const QDBusConnection &connection)
        : m_connection(connection)
    {
        m_properties.insert(QStringLiteral("ActiveState"), QStringLiteral("inactive"));
    }

    void deferGetAll()
    {
        m_deferGetAll = true;
    }

    int pendingGetAllCount() const
    {
        return m_pendingGetAll.count();
    }

    void replyNextGetAll()
    {
        QVERIFY(!m_pendingGetAll.isEmpty());
        const QDBusMessage request = m_pendingGetAll.takeFirst();
        QVERIFY(m_connection.send(request.createReply(QVariantList() << m_properties)));
    }

    void setActiveState(const QString &state)
    {
        m_properties.insert(QStringLiteral("ActiveState"), state);
    }

    void emitActiveStateChanged(const QString &state)
    {
        setActiveState(state);
        QVariantMap changed;
        changed.insert(QStringLiteral("ActiveState"), state);
        QDBusMessage signal = QDBusMessage::createSignal(
                    QString::fromLatin1(unitPath),
                    QString::fromLatin1(propertiesInterface),
                    QStringLiteral("PropertiesChanged"));
        signal.setArguments(QVariantList()
                            << QString::fromLatin1(unitInterface)
                            << changed
                            << QStringList());
        QVERIFY(m_connection.send(signal));
    }

    QString introspect(const QString &path) const Q_DECL_OVERRIDE
    {
        Q_UNUSED(path)
        return QStringLiteral(
                    "<node>"
                    "<interface name=\"org.freedesktop.DBus.Properties\">"
                    "<method name=\"GetAll\">"
                    "<arg name=\"interface\" type=\"s\" direction=\"in\"/>"
                    "<arg name=\"properties\" type=\"a{sv}\" direction=\"out\"/>"
                    "</method>"
                    "</interface>"
                    "</node>");
    }

    bool handleMessage(const QDBusMessage &request,
                       const QDBusConnection &connection) Q_DECL_OVERRIDE
    {
        if (request.type() != QDBusMessage::MethodCallMessage ||
                request.interface() != QString::fromLatin1(propertiesInterface) ||
                request.member() != QStringLiteral("GetAll") ||
                request.arguments().count() != 1 ||
                request.arguments().first().toString() != QString::fromLatin1(unitInterface)) {
            return false;
        }
        if (m_deferGetAll) {
            m_pendingGetAll.append(request);
            return true;
        }
        return connection.send(request.createReply(QVariantList() << m_properties));
    }

private:
    QDBusConnection m_connection;
    bool m_deferGetAll = false;
    QList<QDBusMessage> m_pendingGetAll;
    QVariantMap m_properties;
};

static void registerSystemd(QDBusConnection &connection,
                            DelayedSystemdManager *manager,
                            DelayedUnitProperties *properties,
                            QDBusConnectionInterface::ServiceQueueOptions queue =
                                QDBusConnectionInterface::DontQueueService,
                            QDBusConnectionInterface::ServiceReplacementOptions replacement =
                                QDBusConnectionInterface::DontAllowReplacement)
{
    QVERIFY(connection.registerObject(QString::fromLatin1(managerPath), manager,
                                      QDBusConnection::ExportAllSlots));
    QVERIFY(connection.registerVirtualObject(QString::fromLatin1(unitPath), properties));
    QVERIFY(connection.interface()->registerService(QString::fromLatin1(serviceName), queue,
                                                    replacement).isValid());
}

static void unregisterSystemd(QDBusConnection &connection)
{
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
    connection.unregisterObject(QString::fromLatin1(managerPath));
    connection.unregisterObject(QString::fromLatin1(unitPath));
}

static void waitForReady(ServiceControl *control)
{
    QTRY_VERIFY(control->ready());
    QVERIFY(!control->serviceRunning());
}

static void verifyPromptReturn(QElapsedTimer *timer, const char *message)
{
    QVERIFY2(timer->elapsed() < 1000, message);
}

class ServiceControlAsyncTest : public QObject
{
    Q_OBJECT

private slots:
    void constructorDoesNotBlockOnDelayedBootstrap();
    void activeStateSignalBeatsStaleGetAll();
    void startChainIsAsyncAndOrdered();
    void stopChainIsAsyncAndOrdered();
    void restartIsAsync();
    void stopRequestedDuringStartWaitsForStartChain();
    void lateOldOwnerPropertiesSignalIsIgnored();
};

void ServiceControlAsyncTest::constructorDoesNotBlockOnDelayedBootstrap()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-bootstrap"));
    QVERIFY(connection.isConnected());
    DelayedSystemdManager manager(connection);
    DelayedUnitProperties properties(connection);
    manager.defer(QStringLiteral("Subscribe"));
    manager.defer(QStringLiteral("LoadUnit"));
    properties.deferGetAll();
    registerSystemd(connection, &manager, &properties);

    QElapsedTimer timer;
    timer.start();
    ServiceControl control;
    verifyPromptReturn(&timer, "ServiceControl construction blocked on a systemd reply");
    QVERIFY(!control.ready());

    QTRY_COMPARE(manager.pendingCount(QStringLiteral("Subscribe")), 1);
    manager.replyNext(QStringLiteral("Subscribe"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("LoadUnit")), 1);
    manager.replyNext(QStringLiteral("LoadUnit"));
    QTRY_COMPARE(properties.pendingGetAllCount(), 1);
    QVERIFY(!control.ready());

    properties.replyNextGetAll();
    waitForReady(&control);
    unregisterSystemd(connection);
}

void ServiceControlAsyncTest::activeStateSignalBeatsStaleGetAll()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-stale-state"));
    QVERIFY(connection.isConnected());
    DelayedSystemdManager manager(connection);
    DelayedUnitProperties properties(connection);
    properties.deferGetAll();
    registerSystemd(connection, &manager, &properties);

    ServiceControl control;
    QTRY_COMPARE(properties.pendingGetAllCount(), 1);
    QVERIFY(!control.ready());

    // Wait until the client-side signal match is installed before emitting the authoritative
    // update. The held GetAll result below deliberately contains the old state.
    QTest::qWait(20);
    properties.emitActiveStateChanged(QStringLiteral("active"));
    QTRY_VERIFY(control.serviceRunning());

    properties.setActiveState(QStringLiteral("inactive"));
    properties.replyNextGetAll();
    QTest::qWait(50);
    QVERIFY(control.serviceRunning());
    unregisterSystemd(connection);
}

void ServiceControlAsyncTest::lateOldOwnerPropertiesSignalIsIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedSystemdManager firstManager(firstConnection);
    DelayedUnitProperties firstProperties(firstConnection);
    registerSystemd(firstConnection, &firstManager, &firstProperties,
                   QDBusConnectionInterface::DontQueueService,
                   QDBusConnectionInterface::AllowReplacement);

    ServiceControl control;
    waitForReady(&control);
    SystemdUnitPropertiesInterface *oldPropertiesInterface =
            control.findChild<SystemdUnitPropertiesInterface *>();
    QVERIFY(oldPropertiesInterface);

    bool injectedOldSignal = false;
    QObject::connect(&control, &ServiceControl::readyChanged, [&control,
                                                                 oldPropertiesInterface,
                                                                 &injectedOldSignal]() {
        if (control.ready() || injectedOldSignal) {
            return;
        }
        QVariantMap changed;
        changed.insert(QStringLiteral("ActiveState"), QStringLiteral("active"));
        injectedOldSignal = QMetaObject::invokeMethod(
                    oldPropertiesInterface, "PropertiesChanged", Qt::DirectConnection,
                    Q_ARG(QString, QString::fromLatin1(unitInterface)),
                    Q_ARG(QVariantMap, changed), Q_ARG(QStringList, QStringList()));
    });

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedSystemdManager secondManager(secondConnection);
    DelayedUnitProperties secondProperties(secondConnection);
    secondProperties.deferGetAll();
    registerSystemd(secondConnection, &secondManager, &secondProperties,
                   QDBusConnectionInterface::ReplaceExistingService,
                   QDBusConnectionInterface::DontAllowReplacement);

    QTRY_VERIFY(injectedOldSignal);
    QTRY_COMPARE(secondProperties.pendingGetAllCount(), 1);
    QVERIFY(!control.ready());
    QVERIFY(!control.serviceRunning());

    secondProperties.replyNextGetAll();
    waitForReady(&control);
    unregisterSystemd(secondConnection);
    firstConnection.unregisterObject(QString::fromLatin1(managerPath));
    firstConnection.unregisterObject(QString::fromLatin1(unitPath));
}

void ServiceControlAsyncTest::startChainIsAsyncAndOrdered()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-start"));
    QVERIFY(connection.isConnected());
    DelayedSystemdManager manager(connection);
    DelayedUnitProperties properties(connection);
    registerSystemd(connection, &manager, &properties);

    ServiceControl control;
    waitForReady(&control);
    manager.clearCalls();
    manager.defer(QStringLiteral("EnableUnitFiles"));
    manager.defer(QStringLiteral("Reload"));
    manager.defer(QStringLiteral("StartUnit"));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(control.startService());
    verifyPromptReturn(&timer, "startService blocked on EnableUnitFiles");
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("EnableUnitFiles")), 1);
    QCOMPARE(manager.calls(), QStringList() << QStringLiteral("EnableUnitFiles"));

    manager.replyNext(QStringLiteral("EnableUnitFiles"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("Reload")), 1);
    QCOMPARE(manager.calls(), QStringList()
             << QStringLiteral("EnableUnitFiles") << QStringLiteral("Reload"));

    manager.replyNext(QStringLiteral("Reload"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("StartUnit")), 1);
    QCOMPARE(manager.calls(), QStringList()
             << QStringLiteral("EnableUnitFiles") << QStringLiteral("Reload")
             << QStringLiteral("StartUnit"));

    manager.replyNext(QStringLiteral("StartUnit"));
    unregisterSystemd(connection);
}

void ServiceControlAsyncTest::stopChainIsAsyncAndOrdered()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-stop"));
    QVERIFY(connection.isConnected());
    DelayedSystemdManager manager(connection);
    DelayedUnitProperties properties(connection);
    registerSystemd(connection, &manager, &properties);

    ServiceControl control;
    waitForReady(&control);
    manager.clearCalls();
    manager.defer(QStringLiteral("StopUnit"));
    manager.defer(QStringLiteral("DisableUnitFiles"));
    manager.defer(QStringLiteral("Reload"));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(control.stopService());
    verifyPromptReturn(&timer, "stopService blocked on StopUnit");
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("StopUnit")), 1);
    QCOMPARE(manager.calls(), QStringList() << QStringLiteral("StopUnit"));

    manager.replyNext(QStringLiteral("StopUnit"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("DisableUnitFiles")), 1);
    QCOMPARE(manager.calls(), QStringList()
             << QStringLiteral("StopUnit") << QStringLiteral("DisableUnitFiles"));

    manager.replyNext(QStringLiteral("DisableUnitFiles"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("Reload")), 1);
    QCOMPARE(manager.calls(), QStringList()
             << QStringLiteral("StopUnit") << QStringLiteral("DisableUnitFiles")
             << QStringLiteral("Reload"));

    manager.replyNext(QStringLiteral("Reload"));
    unregisterSystemd(connection);
}

void ServiceControlAsyncTest::restartIsAsync()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-restart"));
    QVERIFY(connection.isConnected());
    DelayedSystemdManager manager(connection);
    DelayedUnitProperties properties(connection);
    registerSystemd(connection, &manager, &properties);

    ServiceControl control;
    waitForReady(&control);
    manager.clearCalls();
    manager.defer(QStringLiteral("RestartUnit"));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(control.restartService());
    verifyPromptReturn(&timer, "restartService blocked on RestartUnit");
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("RestartUnit")), 1);
    QCOMPARE(manager.calls(), QStringList() << QStringLiteral("RestartUnit"));

    manager.replyNext(QStringLiteral("RestartUnit"));
    unregisterSystemd(connection);
}

void ServiceControlAsyncTest::stopRequestedDuringStartWaitsForStartChain()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("servicecontrol-async-overlap"));
    QVERIFY(connection.isConnected());
    DelayedSystemdManager manager(connection);
    DelayedUnitProperties properties(connection);
    registerSystemd(connection, &manager, &properties);

    ServiceControl control;
    waitForReady(&control);
    manager.clearCalls();
    manager.defer(QStringLiteral("EnableUnitFiles"));
    manager.defer(QStringLiteral("Reload"));
    manager.defer(QStringLiteral("StartUnit"));
    manager.defer(QStringLiteral("StopUnit"));
    manager.defer(QStringLiteral("DisableUnitFiles"));

    QVERIFY(control.startService());
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("EnableUnitFiles")), 1);
    QVERIFY(control.stopService());
    QCOMPARE(manager.pendingCount(QStringLiteral("StopUnit")), 0);

    manager.replyNext(QStringLiteral("EnableUnitFiles"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("Reload")), 1);
    QCOMPARE(manager.pendingCount(QStringLiteral("StopUnit")), 0);

    manager.replyNext(QStringLiteral("Reload"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("StartUnit")), 1);
    QCOMPARE(manager.pendingCount(QStringLiteral("StopUnit")), 0);

    manager.replyNext(QStringLiteral("StartUnit"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("StopUnit")), 1);
    QCOMPARE(manager.calls(), QStringList()
             << QStringLiteral("EnableUnitFiles") << QStringLiteral("Reload")
             << QStringLiteral("StartUnit") << QStringLiteral("StopUnit"));

    manager.replyNext(QStringLiteral("StopUnit"));
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("DisableUnitFiles")), 1);
    manager.replyNext(QStringLiteral("DisableUnitFiles"));
    // Reload was delayed by the start chain too, so the queued stop must issue a second request.
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("Reload")), 1);
    manager.replyNext(QStringLiteral("Reload"));
    unregisterSystemd(connection);
}
}

QTEST_MAIN(ServiceControlAsyncTest)
#include "servicecontrol_async_test.moc"
