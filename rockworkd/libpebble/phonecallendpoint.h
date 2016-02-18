#ifndef PHONECALLENDPOINT_H
#define PHONECALLENDPOINT_H

#include <QObject>

class Pebble;
class WatchConnection;

class PhoneCallEndpoint : public QObject
{
    Q_OBJECT
public:
    enum CallAction{
        CallActionAnswer = 1,
        CallActionHangup = 2,
        CallActionGetState = 3,
        CallActionIncoming = 4,
        CallActionOutgoing = 5,
        CallActionMissed = 6,
        CallActionRing = 7,
        CallActionStart = 8,
        CallActionEnd = 9
    };

    explicit PhoneCallEndpoint(Pebble *pebble, WatchConnection *connection);

public slots:
    void incomingCall(uint cookie, const QString &number, const QString &name);
    void callStarted(uint cookie);
    void callEnded(uint cookie, bool missed);

signals:
    void hangupCall(uint cookie);


private:
    void phoneControl(char act, uint cookie, QStringList datas);

private slots:
    void handlePhoneEvent(const QByteArray &data);

private:
    Pebble *m_pebble;
    WatchConnection *m_connection;
};

#endif // PHONECALLENDPOINT_H
