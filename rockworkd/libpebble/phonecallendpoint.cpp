#include "phonecallendpoint.h"

#include "pebble.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

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
    WatchDataWriter w(&head);
    head.append(act);
    w.write<quint32>(cookie);
    switch (act) {
    case CallActionMissed:
    case CallActionIncoming:
        if (datas.length()>1) {
            w.writePascalString(datas.at(0)); // name
            w.writePascalString(datas.at(1)); // number
        } else {
            qWarning() << "Empty payload not expected, filling with dummy values";
            w.writePascalString("empty number"); // name
            w.writePascalString("+000 000 000 000"); // number
        }
        break;
    default:
        break;
    }
    m_connection->writeToPebble(WatchConnection::EndpointPhoneControl, head);
}

void PhoneCallEndpoint::handlePhoneEvent(const QByteArray &data)
{

    WatchDataReader reader(data);
    quint8 command = reader.read<quint8>();
    quint32 cookie = reader.read<quint32>();
    QList<CallState> res;
    quint8 len;

    switch(command) {
    case CallActionAnswer:
        emit answerCall(cookie);
        break;
    case CallActionHangup:
        emit hangupCall(cookie);
        break;
    case CallActionResState:
        while(!reader.checkBad()) {
            CallState cs;
            len = reader.read<quint8>();
            if(len != sizeof(CallState)) {
                qWarning() << "Data corruption:" << len << "does not match expected payload size" << sizeof(CallState);
                break;
            }
            cs.action = reader.read<quint8>();
            cs.cookie = reader.read<quint32>();
            res.append(cs);
        }
        emit callState(cookie, res);
        break;
    default:
        qWarning() << "received an unhandled phone event" << data.toHex();
    }
}
