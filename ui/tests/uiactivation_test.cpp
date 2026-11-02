#include "../uiactivation.h"
#include <QtTest>
#include <QDataStream>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>

class UiActivationTest : public QObject
{
    Q_OBJECT
private slots:
    void notificationActionOpensMatchingWatch()
    {
        const QByteArray bytes = QByteArray::fromBase64(
            "AAAACgAAAAAiADAAMQA6ADIAMwA6ADQANQA6ADYANwA6ADgAOQA6AEEAQg==");
        QDataStream stream(bytes);
        QVariant address;
        stream >> address;
        QCOMPARE(address.type(), QVariant::String);
        QCOMPARE(address.toString(), QStringLiteral("01:23:45:67:89:AB"));
        UiActivation activation;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerObject("/org/rockpool/UI", &activation, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService("org.rockpool.UI"));
        QSignalSpy requested(&activation, &UiActivation::firmwareRequested);
        auto call = QDBusMessage::createMethodCall("org.rockpool.UI", "/org/rockpool/UI",
                                                 "org.rockpool.UI", "OpenFirmwareUpdate");
        call << address.toString();
        QDBusPendingReply<> reply = bus.asyncCall(call);
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QCOMPARE(requested.count(), 1);
        QCOMPARE(activation.firmwareAddress(), address.toString());
        activation.consumeFirmwareRequest();
        QVERIFY(activation.firmwareAddress().isEmpty());
        activation.OpenFirmwareUpdate("invalid");
        QCOMPARE(requested.count(), 1);
        activation.OpenFirmwareUpdate(address.toString().toLower());
        QCOMPARE(requested.count(), 2);
        QCOMPARE(activation.firmwareAddress(), address.toString());
        bus.unregisterObject("/org/rockpool/UI");
        bus.unregisterService("org.rockpool.UI");
    }
};

QTEST_GUILESS_MAIN(UiActivationTest)
#include "uiactivation_test.moc"
