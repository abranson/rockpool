/*
 * Regression coverage for asynchronous io.rebble.libpebble3.Operation1 monitoring.
 *
 * Run with run-pebbles-async-test.sh so io.rebble.libpebble3 is private to this test.
 */
#include "rockpooloperation.h"

#include <QtTest>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QList>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QVariantMap>

namespace {
const char serviceName[] = "io.rebble.libpebble3";
const char operationPath[] = "/io/rebble/libpebble3/Operations/test";
const char operationInterface[] = "io.rebble.libpebble3.Operation1";
const char propertiesInterface[] = "org.freedesktop.DBus.Properties";

static QVariantMap operationProperties(const QString &state,
                                       double progress = 0.0,
                                       const QVariantMap &result = QVariantMap(),
                                       const QString &error = QString(),
                                       const QString &errorDetail = QString())
{
    QVariantMap properties;
    properties.insert(QStringLiteral("Kind"), QStringLiteral("test.operation"));
    properties.insert(QStringLiteral("State"), state);
    properties.insert(QStringLiteral("Progress"), progress);
    properties.insert(QStringLiteral("Result"), result);
    properties.insert(QStringLiteral("Error"), error);
    properties.insert(QStringLiteral("ErrorDetail"), errorDetail);
    return properties;
}

class DelayedOperation : public QDBusVirtualObject
{
public:
    explicit DelayedOperation(const QDBusConnection &connection)
        : m_connection(connection),
          m_properties(operationProperties(QStringLiteral("pending")))
    {
    }

    void setProperties(const QVariantMap &properties)
    {
        m_properties = properties;
    }

    void deferGetAll()
    {
        m_deferGetAll = true;
    }

    void deferCancel()
    {
        m_deferCancel = true;
    }

    int pendingGetAllCount() const
    {
        return m_pendingGetAll.count();
    }

    int pendingCancelCount() const
    {
        return m_pendingCancel.count();
    }

    int cancelCount() const
    {
        return m_cancelCount;
    }

    void replyNextGetAll(const QVariantMap &properties)
    {
        replyGetAll(0, properties);
    }

    void replyGetAll(int index, const QVariantMap &properties)
    {
        QVERIFY(index >= 0 && index < m_pendingGetAll.count());
        const QDBusMessage request = m_pendingGetAll.takeAt(index);
        QVERIFY(m_connection.send(request.createReply(QVariantList() << properties)));
    }

    void replyNextGetAllError(const QString &name, const QString &message)
    {
        QVERIFY(!m_pendingGetAll.isEmpty());
        const QDBusMessage request = m_pendingGetAll.takeFirst();
        QVERIFY(m_connection.send(request.createErrorReply(name, message)));
    }

    void replyNextCancel()
    {
        QVERIFY(!m_pendingCancel.isEmpty());
        const QDBusMessage request = m_pendingCancel.takeFirst();
        QVERIFY(m_connection.send(request.createReply()));
    }

    void emitPropertiesChanged(const QVariantMap &changed)
    {
        for (QVariantMap::const_iterator it = changed.constBegin(); it != changed.constEnd(); ++it) {
            m_properties.insert(it.key(), it.value());
        }
        QDBusMessage signal = QDBusMessage::createSignal(
                    QString::fromLatin1(operationPath),
                    QString::fromLatin1(propertiesInterface),
                    QStringLiteral("PropertiesChanged"));
        signal.setArguments(QVariantList()
                            << QString::fromLatin1(operationInterface)
                            << changed
                            << QStringList());
        QVERIFY(m_connection.send(signal));
    }

    void emitCompleted(bool success)
    {
        QDBusMessage signal = QDBusMessage::createSignal(
                    QString::fromLatin1(operationPath),
                    QString::fromLatin1(operationInterface),
                    QStringLiteral("Completed"));
        signal.setArguments(QVariantList() << success);
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
                    "<interface name=\"io.rebble.libpebble3.Operation1\">"
                    "<method name=\"Cancel\"/>"
                    "<signal name=\"Completed\"><arg name=\"success\" type=\"b\"/></signal>"
                    "</interface>"
                    "</node>");
    }

    bool handleMessage(const QDBusMessage &request,
                       const QDBusConnection &connection) Q_DECL_OVERRIDE
    {
        if (request.type() != QDBusMessage::MethodCallMessage) {
            return false;
        }
        if (request.interface() == QString::fromLatin1(propertiesInterface) &&
                request.member() == QStringLiteral("GetAll") &&
                request.arguments().count() == 1 &&
                request.arguments().first().toString() == QString::fromLatin1(operationInterface)) {
            if (m_deferGetAll) {
                m_pendingGetAll.append(request);
                return true;
            }
            return connection.send(request.createReply(QVariantList() << m_properties));
        }
        if (request.interface() == QString::fromLatin1(operationInterface) &&
                request.member() == QStringLiteral("Cancel") && request.arguments().isEmpty()) {
            ++m_cancelCount;
            if (m_deferCancel) {
                m_pendingCancel.append(request);
                return true;
            }
            return connection.send(request.createReply());
        }
        return false;
    }

private:
    QDBusConnection m_connection;
    QVariantMap m_properties;
    bool m_deferGetAll = false;
    bool m_deferCancel = false;
    int m_cancelCount = 0;
    QList<QDBusMessage> m_pendingGetAll;
    QList<QDBusMessage> m_pendingCancel;
};

static void registerOperation(QDBusConnection &connection, DelayedOperation *operation,
                              QDBusConnectionInterface::ServiceQueueOptions queue =
                                  QDBusConnectionInterface::DontQueueService,
                              QDBusConnectionInterface::ServiceReplacementOptions replacement =
                                  QDBusConnectionInterface::DontAllowReplacement)
{
    QVERIFY(connection.registerVirtualObject(QString::fromLatin1(operationPath), operation));
    QVERIFY(connection.interface()->registerService(QString::fromLatin1(serviceName), queue,
                                                    replacement).isValid());
}

static void unregisterOperation(QDBusConnection &connection)
{
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
    connection.unregisterObject(QString::fromLatin1(operationPath));
}

class RockpoolOperationTest : public QObject
{
    Q_OBJECT

private slots:
    void terminalBeforeSubscribeCompletesFromGetAll();
    void terminalSignalBeatsStaleGetAll();
    void terminalSignalSurvivesFailedReadback();
    void propertiesChangedUpdatesProgressAndTerminalPayload();
    void ownerReplacementAndLossInvalidateOldRequests();
    void cancelIsAsyncAndIdempotent();
    void invalidSnapshotsRetryOnceThenFail();
    void signalDuringRetrySupersedesTheTimer();
};

void RockpoolOperationTest::terminalBeforeSubscribeCompletesFromGetAll()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-terminal-snapshot"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    QVariantMap result;
    result.insert(QStringLiteral("address"), QStringLiteral("01:23:45:67:89:ab"));
    fake.setProperties(operationProperties(QStringLiteral("succeeded"), 1.0, result));
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_VERIFY(operation.ready());
    QVERIFY(operation.available());
    QTRY_VERIFY(operation.finished());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.takeFirst().at(0).toBool(), true);
    QCOMPARE(operation.path(), QString::fromLatin1(operationPath));
    QVERIFY(operation.success());
    QCOMPARE(operation.kind(), QStringLiteral("test.operation"));
    QCOMPARE(operation.state(), QStringLiteral("succeeded"));
    QCOMPARE(operation.progress(), 1.0);
    QCOMPARE(operation.result(), result);
    QCOMPARE(operation.error(), QString());
    QCOMPARE(operation.errorDetail(), QString());
    unregisterOperation(connection);
}

void RockpoolOperationTest::terminalSignalBeatsStaleGetAll()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-stale-getall"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    fake.deferGetAll();
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);

    // Let the client install its signal matches before publishing the authoritative terminal state.
    QTest::qWait(20);
    QVariantMap result;
    result.insert(QStringLiteral("imported"), 1);
    QVariantMap terminal = operationProperties(QStringLiteral("succeeded"), 1.0, result);
    fake.emitPropertiesChanged(terminal);
    fake.emitCompleted(true);
    QCOMPARE(fake.pendingGetAllCount(), 1);
    fake.replyNextGetAll(operationProperties(QStringLiteral("running"), 0.25));
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);
    fake.replyNextGetAll(terminal);
    QTRY_VERIFY(operation.finished());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(operation.state(), QStringLiteral("succeeded"));
    QCOMPARE(operation.progress(), 1.0);
    QCOMPARE(operation.result(), result);
    QCOMPARE(completed.count(), 1);
    unregisterOperation(connection);
}

void RockpoolOperationTest::terminalSignalSurvivesFailedReadback()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-terminal-readback-error"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    fake.deferGetAll();
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);

    // Let the client install its signal matches before publishing the authoritative terminal state.
    QTest::qWait(20);
    QVariantMap result;
    result.insert(QStringLiteral("source"), QStringLiteral("properties-changed"));
    const QVariantMap terminal = operationProperties(
                QStringLiteral("succeeded"), 1.0, result);
    fake.emitPropertiesChanged(terminal);
    fake.emitCompleted(true);

    // The first request predates the terminal signal. Its stale result makes the client
    // request an authoritative terminal readback, which can fail if the daemon evicts it.
    fake.replyNextGetAll(operationProperties(QStringLiteral("running"), 0.25));
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);
    fake.replyNextGetAllError(QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"),
                              QStringLiteral("operation was evicted"));

    QTRY_VERIFY(operation.ready());
    QVERIFY(operation.available());
    QTRY_VERIFY(operation.finished());
    QVERIFY(operation.success());
    QCOMPARE(operation.state(), QStringLiteral("succeeded"));
    QCOMPARE(operation.progress(), 1.0);
    QCOMPARE(operation.result(), result);
    QCOMPARE(operation.error(), QString());
    QCOMPARE(operation.errorDetail(), QString());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.takeFirst().at(0).toBool(), true);
    QTest::qWait(350);
    QCOMPARE(fake.pendingGetAllCount(), 0);
    QCOMPARE(completed.count(), 0);
    unregisterOperation(connection);
}

void RockpoolOperationTest::propertiesChangedUpdatesProgressAndTerminalPayload()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-properties"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    fake.setProperties(operationProperties(QStringLiteral("running"), 0.0));
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy changed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::changed));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_VERIFY(operation.ready());

    QVariantMap progress;
    progress.insert(QStringLiteral("Progress"), 0.5);
    fake.emitPropertiesChanged(progress);
    QTRY_COMPARE(operation.progress(), 0.5);

    QVariantMap result;
    result.insert(QStringLiteral("retryable"), false);
    QVariantMap terminal;
    terminal.insert(QStringLiteral("State"), QStringLiteral("failed"));
    terminal.insert(QStringLiteral("Progress"), 1.0);
    terminal.insert(QStringLiteral("Result"), result);
    terminal.insert(QStringLiteral("Error"), QStringLiteral("io.rebble.libpebble3.Error.Internal"));
    terminal.insert(QStringLiteral("ErrorDetail"), QStringLiteral("test failure"));
    fake.emitPropertiesChanged(terminal);
    fake.emitCompleted(false);
    QTRY_VERIFY(operation.finished());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.takeFirst().at(0).toBool(), false);
    QVERIFY(!operation.success());
    QCOMPARE(operation.state(), QStringLiteral("failed"));
    QCOMPARE(operation.progress(), 1.0);
    QCOMPARE(operation.result(), result);
    QCOMPARE(operation.error(), QStringLiteral("io.rebble.libpebble3.Error.Internal"));
    QCOMPARE(operation.errorDetail(), QStringLiteral("test failure"));
    QVERIFY(changed.count() >= 2);
    unregisterOperation(connection);
}

void RockpoolOperationTest::ownerReplacementAndLossInvalidateOldRequests()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedOperation firstFake(firstConnection);
    firstFake.deferGetAll();
    registerOperation(firstConnection, &firstFake,
                      QDBusConnectionInterface::DontQueueService,
                      QDBusConnectionInterface::AllowReplacement);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_COMPARE(firstFake.pendingGetAllCount(), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedOperation secondFake(secondConnection);
    secondFake.deferGetAll();
    registerOperation(secondConnection, &secondFake,
                      QDBusConnectionInterface::ReplaceExistingService,
                      QDBusConnectionInterface::DontAllowReplacement);
    QTRY_VERIFY(operation.finished());
    QVERIFY(operation.ready());
    QVERIFY(!operation.available());
    QVERIFY(!operation.success());
    QCOMPARE(operation.state(), QStringLiteral("failed"));
    QCOMPARE(completed.count(), 1);

    firstFake.replyNextGetAll(operationProperties(QStringLiteral("succeeded"), 1.0));
    QTest::qWait(50);
    QVERIFY(operation.finished());
    QVERIFY(!operation.available());
    QCOMPARE(operation.state(), QStringLiteral("failed"));
    QCOMPARE(completed.count(), 1);

    QVERIFY(secondConnection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
    QTest::qWait(50);
    QCOMPARE(completed.count(), 1);
    firstConnection.unregisterObject(QString::fromLatin1(operationPath));
    secondConnection.unregisterObject(QString::fromLatin1(operationPath));
}

void RockpoolOperationTest::cancelIsAsyncAndIdempotent()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-cancel"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    fake.setProperties(operationProperties(QStringLiteral("running"), 0.25));
    fake.deferCancel();
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_VERIFY(operation.ready());

    QElapsedTimer timer;
    timer.start();
    operation.cancel();
    QVERIFY2(timer.elapsed() < 1000, "cancel blocked on a D-Bus reply");
    QTRY_COMPARE(fake.cancelCount(), 1);
    QCOMPARE(fake.pendingCancelCount(), 1);
    operation.cancel();
    QTest::qWait(50);
    QCOMPARE(fake.cancelCount(), 1);

    fake.replyNextCancel();
    QTest::qWait(50);
    QVERIFY(!operation.finished());
    QVariantMap terminal;
    terminal.insert(QStringLiteral("State"), QStringLiteral("cancelled"));
    terminal.insert(QStringLiteral("Error"), QStringLiteral("io.rebble.libpebble3.Error.Cancelled"));
    terminal.insert(QStringLiteral("ErrorDetail"), QStringLiteral("cancelled"));
    fake.emitPropertiesChanged(terminal);
    fake.emitCompleted(false);
    QTRY_VERIFY(operation.finished());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.takeFirst().at(0).toBool(), false);
    QVERIFY(!operation.success());
    QCOMPARE(operation.state(), QStringLiteral("cancelled"));
    unregisterOperation(connection);
}

void RockpoolOperationTest::invalidSnapshotsRetryOnceThenFail()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-invalid"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    fake.deferGetAll();
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);
    QVariantMap invalid = operationProperties(QStringLiteral("running"), 0.5);
    invalid.insert(QStringLiteral("Progress"), QStringLiteral("0.5"));
    fake.replyNextGetAll(invalid);
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);
    fake.replyNextGetAll(invalid);

    QTRY_VERIFY(operation.finished());
    QVERIFY(operation.ready());
    QVERIFY(!operation.available());
    QVERIFY(!operation.success());
    QCOMPARE(operation.error(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    QCOMPARE(completed.count(), 1);
    unregisterOperation(connection);
}

void RockpoolOperationTest::signalDuringRetrySupersedesTheTimer()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("rockpool-operation-retry-signal"));
    QVERIFY(connection.isConnected());
    DelayedOperation fake(connection);
    fake.deferGetAll();
    registerOperation(connection, &fake);

    RockpoolOperation operation(QDBusObjectPath(QString::fromLatin1(operationPath)));
    QSignalSpy completed(&operation, QMetaMethod::fromSignal(&RockpoolOperation::completed));
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);
    QVariantMap invalid = operationProperties(QStringLiteral("running"), 0.5);
    invalid.insert(QStringLiteral("Progress"), QStringLiteral("0.5"));
    fake.replyNextGetAll(invalid);
    QTest::qWait(50);

    QVariantMap result;
    result.insert(QStringLiteral("source"), QStringLiteral("signal"));
    const QVariantMap terminal = operationProperties(
                QStringLiteral("succeeded"), 1.0, result);
    fake.emitPropertiesChanged(terminal);
    QTRY_COMPARE(fake.pendingGetAllCount(), 1);
    fake.replyNextGetAll(terminal);

    QTRY_VERIFY(operation.finished());
    QCOMPARE(operation.result(), result);
    QCOMPARE(completed.count(), 1);
    QTest::qWait(300);
    QCOMPARE(fake.pendingGetAllCount(), 0);
    QCOMPARE(completed.count(), 1);
    unregisterOperation(connection);
}
}

QTEST_MAIN(RockpoolOperationTest)
#include "rockpooloperation_test.moc"
