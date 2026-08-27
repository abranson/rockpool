/*
 * Regression coverage for asynchronous org.rockpool.Manager requests.
 *
 * Run with run-pebbles-async-test.sh so org.rockpool is private to this test.
 */
#include "pebbles.h"
#include "pebble.h"

#include <QtTest>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QElapsedTimer>

namespace {
const char serviceName[] = "org.rockpool";
const char managerPath[] = "/org/rockpool/Manager";

class DelayedManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockpool.Manager")

public:
    explicit DelayedManager(const QDBusConnection &connection)
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

    void replyPending(const QString &method, int index, const QVariantList &arguments)
    {
        QVERIFY2(index >= 0 && index < m_pending.value(method).count(), qPrintable(method));
        const QDBusMessage request = m_pending[method].takeAt(index);
        QVERIFY(m_connection.send(request.createReply(arguments)));
    }

    void replyNext(const QString &method, const QVariantList &arguments)
    {
        replyPending(method, 0, arguments);
    }

    void setWatchPaths(const QList<QDBusObjectPath> &paths)
    {
        m_watchPaths = paths;
    }

    void setScanResultName(const QString &name)
    {
        m_scanResultName = name;
    }

    static QVariantList scanArgumentsForName(const QString &name)
    {
        QDBusArgument result;
        result.beginMap(QMetaType::QString, qMetaTypeId<QDBusVariant>());
        result.beginMapEntry();
        result << QStringLiteral("name") << QDBusVariant(name);
        result.endMapEntry();
        result.endMap();

        QDBusArgument results;
        results.beginArray(qMetaTypeId<QDBusVariant>());
        results << QDBusVariant(QVariant::fromValue(result));
        results.endArray();
        return QVariantList() << QVariant::fromValue(results);
    }

public slots:
    void ListWatches() { replyOrDelay(QStringLiteral("ListWatches"), watchArguments()); }
    void ScanResults() { replyOrDelay(QStringLiteral("ScanResults"), scanArguments()); }
    void Version() { replyOrDelay(QStringLiteral("Version"), QVariantList() << QStringLiteral("test")); }
    void IsScanning() { replyOrDelay(QStringLiteral("IsScanning"), QVariantList() << false); }
    void StartScan() { replyOrDelay(QStringLiteral("StartScan"), QVariantList()); }
    void StopScan() { replyOrDelay(QStringLiteral("StopScan"), QVariantList()); }
    void ConnectWatch(const QString &) { replyOrDelay(QStringLiteral("ConnectWatch"), QVariantList()); }
    void DisconnectWatch(const QString &) { replyOrDelay(QStringLiteral("DisconnectWatch"), QVariantList()); }
    void ForgetWatch(const QString &) { replyOrDelay(QStringLiteral("ForgetWatch"), QVariantList()); }

private:
    QVariantList watchArguments() const
    {
        return QVariantList() << QVariant::fromValue(m_watchPaths);
    }

    QVariantList scanArguments() const
    {
        return scanArgumentsForName(m_scanResultName);
    }

    void replyOrDelay(const QString &method, const QVariantList &arguments)
    {
        setDelayedReply(true);
        if (m_deferred.contains(method)) {
            m_pending[method].append(message());
            return;
        }
        m_connection.send(message().createReply(arguments));
    }

    QDBusConnection m_connection;
    QStringList m_deferred;
    QHash<QString, QList<QDBusMessage> > m_pending;
    QList<QDBusObjectPath> m_watchPaths;
    QString m_scanResultName;
};

class PebblesAsyncTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void delayedRepliesFromReplacedOwnerAreIgnored();
    void newestSameOwnerScanReplyWins();
    void commandsDoNotWaitForReplies();
};

void PebblesAsyncTest::initTestCase()
{
    qDBusRegisterMetaType<QList<QDBusObjectPath> >();
}

void PebblesAsyncTest::delayedRepliesFromReplacedOwnerAreIgnored()
{
    QDBusConnection firstConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebbles-async-old-owner"));
    QVERIFY(firstConnection.isConnected());
    DelayedManager firstManager(firstConnection);
    QVERIFY(firstConnection.registerObject(QString::fromLatin1(managerPath), &firstManager,
                                            QDBusConnection::ExportAllSlots));
    QVERIFY(firstConnection.interface()->registerService(
                QString::fromLatin1(serviceName),
                QDBusConnectionInterface::DontQueueService,
                QDBusConnectionInterface::AllowReplacement).isValid());

    Pebbles pebbles;
    QTRY_VERIFY(pebbles.connectedToService());

    firstManager.defer(QStringLiteral("ListWatches"));
    firstManager.defer(QStringLiteral("ScanResults"));
    QMetaObject::invokeMethod(&pebbles, "refresh");
    QMetaObject::invokeMethod(&pebbles, "refreshScanResults");
    QTRY_COMPARE(firstManager.pendingCount(QStringLiteral("ListWatches")), 1);
    QTRY_COMPARE(firstManager.pendingCount(QStringLiteral("ScanResults")), 1);

    QDBusConnection secondConnection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebbles-async-new-owner"));
    QVERIFY(secondConnection.isConnected());
    DelayedManager secondManager(secondConnection);
    secondManager.setWatchPaths(QList<QDBusObjectPath>());
    secondManager.defer(QStringLiteral("ScanResults"));
    QVERIFY(secondConnection.registerObject(QString::fromLatin1(managerPath), &secondManager,
                                             QDBusConnection::ExportAllSlots));
    QVERIFY(secondConnection.interface()->registerService(
                QString::fromLatin1(serviceName),
                QDBusConnectionInterface::ReplaceExistingService,
                QDBusConnectionInterface::DontAllowReplacement).isValid());

    QTRY_COMPARE(secondManager.pendingCount(QStringLiteral("ScanResults")), 1);
    secondManager.replyNext(QStringLiteral("ScanResults"),
                            DelayedManager::scanArgumentsForName(QStringLiteral("fresh")));
    QTRY_VERIFY(pebbles.connectedToService());
    QTRY_COMPARE(pebbles.rowCount(), 0);
    QTRY_VERIFY(!pebbles.scanResults().isEmpty());
    QTRY_COMPARE(pebbles.scanResults().first().toMap().value("name").toString(),
                 QStringLiteral("fresh"));

    firstManager.replyNext(QStringLiteral("ListWatches"), QVariantList() <<
                           QVariant::fromValue(QList<QDBusObjectPath>() << QDBusObjectPath("/stale")));
    firstManager.replyNext(QStringLiteral("ScanResults"),
                           DelayedManager::scanArgumentsForName(QStringLiteral("stale")));
    QTest::qWait(50);

    QCOMPARE(pebbles.rowCount(), 0);
    QCOMPARE(pebbles.scanResults().first().toMap().value("name").toString(),
             QStringLiteral("fresh"));

    QVERIFY(secondConnection.interface()->unregisterService(
                QString::fromLatin1(serviceName)).isValid());
}

void PebblesAsyncTest::newestSameOwnerScanReplyWins()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebbles-async-order-owner"));
    QVERIFY(connection.isConnected());
    DelayedManager manager(connection);
    QVERIFY(connection.registerObject(QString::fromLatin1(managerPath), &manager,
                                       QDBusConnection::ExportAllSlots));
    QVERIFY(connection.interface()->registerService(QString::fromLatin1(serviceName)).isValid());

    Pebbles pebbles;
    QTRY_VERIFY(pebbles.connectedToService());
    manager.defer(QStringLiteral("ScanResults"));
    QMetaObject::invokeMethod(&pebbles, "refreshScanResults");
    QMetaObject::invokeMethod(&pebbles, "refreshScanResults");
    QTRY_COMPARE(manager.pendingCount(QStringLiteral("ScanResults")), 2);

    manager.replyPending(QStringLiteral("ScanResults"), 1,
                         DelayedManager::scanArgumentsForName(QStringLiteral("newest")));
    QTRY_VERIFY(!pebbles.scanResults().isEmpty());
    QTRY_COMPARE(pebbles.scanResults().first().toMap().value("name").toString(),
                 QStringLiteral("newest"));

    manager.replyNext(QStringLiteral("ScanResults"),
                      DelayedManager::scanArgumentsForName(QStringLiteral("oldest")));
    QTest::qWait(50);
    QCOMPARE(pebbles.scanResults().first().toMap().value("name").toString(),
             QStringLiteral("newest"));

    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}

void PebblesAsyncTest::commandsDoNotWaitForReplies()
{
    QDBusConnection connection = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("pebbles-async-command-owner"));
    QVERIFY(connection.isConnected());
    DelayedManager manager(connection);
    QVERIFY(connection.registerObject(QString::fromLatin1(managerPath), &manager,
                                       QDBusConnection::ExportAllSlots));
    QVERIFY(connection.interface()->registerService(QString::fromLatin1(serviceName)).isValid());

    Pebbles pebbles;
    QTRY_VERIFY(pebbles.connectedToService());
    const QStringList methods = QStringList()
        << QStringLiteral("StartScan") << QStringLiteral("StopScan")
        << QStringLiteral("ConnectWatch") << QStringLiteral("DisconnectWatch")
        << QStringLiteral("ForgetWatch");
    foreach (const QString &method, methods) {
        manager.defer(method);
    }

    QElapsedTimer timer;
    timer.start(); pebbles.startScan(); QVERIFY2(timer.elapsed() < 100, "StartScan blocked");
    timer.restart(); pebbles.stopScan(); QVERIFY2(timer.elapsed() < 100, "StopScan blocked");
    timer.restart(); pebbles.connectWatch(QStringLiteral("AA:BB")); QVERIFY2(timer.elapsed() < 100, "ConnectWatch blocked");
    timer.restart(); pebbles.disconnectWatch(QStringLiteral("AA:BB")); QVERIFY2(timer.elapsed() < 100, "DisconnectWatch blocked");
    timer.restart(); pebbles.forgetWatch(QStringLiteral("AA:BB")); QVERIFY2(timer.elapsed() < 100, "ForgetWatch blocked");

    foreach (const QString &method, methods) {
        QTRY_COMPARE(manager.pendingCount(method), 1);
        manager.replyNext(method, QVariantList());
    }
    QVERIFY(connection.interface()->unregisterService(QString::fromLatin1(serviceName)).isValid());
}
}

QTEST_MAIN(PebblesAsyncTest)
#include "pebbles_async_test.moc"
