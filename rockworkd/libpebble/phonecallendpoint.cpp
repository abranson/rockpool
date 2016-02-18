#include "phonecallendpoint.h"

#include "pebble.h"
#include "watchconnection.h"
#include "watchdatareader.h"

PhoneCallEndpoint::PhoneCallEndpoint(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointPhoneControl, this, "handlePhoneEvent");
}

void PhoneCallEndpoint::incomingCall(uint cookie, const QString &number, const QString &name)
{
    QStringList tmp;
    tmp.append(number);
    tmp.append(name);

    char act = CallActionIncoming;
    // FIXME: Outgoing calls don't seem to work... Maybe something wrong in the enum?
//    if (!incoming) {
//        act = CallActionOutgoing;
//    }

    phoneControl(act, cookie, tmp);

}

void PhoneCallEndpoint::callStarted(uint cookie)
{
    phoneControl(CallActionStart, cookie, QStringList());
}

void PhoneCallEndpoint::callEnded(uint cookie, bool missed)
{
    Q_UNUSED(missed)
    // FIXME: The watch doesn't seem to react on Missed... So let's always "End" it for now
//    phoneControl(missed ? CallActionMissed : CallActionEnd, cookie, QStringList());
    phoneControl(CallActionEnd, cookie, QStringList());
}

void PhoneCallEndpoint::phoneControl(char act, uint cookie, QStringList datas)
{
    QByteArray head;
    head.append((char)act);
    head.append((cookie >> 24)& 0xFF);
    head.append((cookie >> 16)& 0xFF);
    head.append((cookie >> 8)& 0xFF);
    head.append(cookie & 0xFF);
    if (datas.length()>0) {
        head.append(m_connection->buildData(datas));
    }

    m_connection->writeToPebble(WatchConnection::EndpointPhoneControl, head);
}

void PhoneCallEndpoint::handlePhoneEvent(const QByteArray &data)
{

    WatchDataReader reader(data);
    reader.skip(1);
    uint cookie = reader.read<uint>();

    if (data.at(0) == CallActionHangup) {
        emit hangupCall(cookie);
    } else {
        qWarning() << "received an unhandled phone event" << data.toHex();
    }
}
