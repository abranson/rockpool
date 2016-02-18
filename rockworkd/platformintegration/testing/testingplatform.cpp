#include "testingplatform.h"

#include <QQuickView>
#include <QDebug>
#include <QQmlContext>

TestingPlatform::TestingPlatform(QObject *parent):
    PlatformInterface(parent)
{
    m_view = new QQuickView();
    m_view->rootContext()->setContextProperty("handler", this);
    qmlRegisterUncreatableType<Pebble>("PebbleTest", 1, 0, "Pebble", "Dont");
    m_view->setSource(QUrl("qrc:///testui/Main.qml"));
    m_view->show();
}

void TestingPlatform::sendMusicControlCommand(MusicControlButton command)
{
    qDebug() << "Testing platform received music command from pebble" << command;
}

MusicMetaData TestingPlatform::musicMetaData() const
{
    return MusicMetaData("TestArtist", "TestAlbum", "TestTitle");
}

void TestingPlatform::sendNotification(int type, const QString &from, const QString &subject, const QString &text)
{
    qDebug() << "Injecting mock notification" << type;
    Notification n("test_app_" + QString::number(type));
    n.setSourceName("Test button " + QString::number(type));
    n.setSender(from);
    n.setSubject(subject);
    n.setBody(text);
    n.setActToken("tralala");
    emit notificationReceived(n);
}

void TestingPlatform::fakeIncomingCall(uint cookie, const QString &number, const QString &name)
{
    emit incomingCall(cookie, number, name);
}

void TestingPlatform::endCall(uint cookie, bool missed)
{
    emit callEnded(cookie, missed);
}

void TestingPlatform::hangupCall(uint cookie)
{
    qDebug() << "Testing platform received a hangup call event";
    emit callEnded(cookie, false);
}

QList<CalendarEvent> TestingPlatform::organizerItems() const
{
    return QList<CalendarEvent>();
}

void TestingPlatform::actionTriggered(const QString &actToken)
{
    qDebug() << "action triggered" << actToken;
}
