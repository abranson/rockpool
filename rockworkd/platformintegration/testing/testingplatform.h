#ifndef TESTINGPLATFORM_H
#define TESTINGPLATFORM_H

#include "libpebble/platforminterface.h"

class QQuickView;

class TestingPlatform : public PlatformInterface
{
    Q_OBJECT
public:
    explicit TestingPlatform(QObject *parent = 0);

    void sendMusicControlCommand(MusicControlButton command) override;
    MusicMetaData musicMetaData() const override;

    Q_INVOKABLE void sendNotification(int type, const QString &from, const QString &subject, const QString &text);
    Q_INVOKABLE void fakeIncomingCall(uint cookie, const QString &number, const QString &name);
    Q_INVOKABLE void endCall(uint cookie, bool missed);

    void hangupCall(uint cookie) override;

    QList<CalendarEvent> organizerItems() const override;
    void actionTriggered(const QString &actToken) override;
signals:

private:
    QQuickView *m_view;
};

#endif // TESTINGPLATFORM_H
