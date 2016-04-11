#include "devconnection.h"

#include "pebble.h"
#include "watchconnection.h"

#include <QException>
#include <QWebSocketServer>
#include <QWebSocket>

#include <QDebug>

DevConnection::DevConnection(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection),
    m_qtwsServer(0)
{
    QObject::connect(pebble, &Pebble::devConEnabledChanged, this, &DevConnection::onEnableChanged);
    QObject::connect(pebble, &Pebble::devConListenPortChanged, this, &DevConnection::onPortChanged);
    QObject::connect(pebble, &Pebble::devConCloudEnabledChanged, this, &DevConnection::onCloudEnableChanged);
    QObject::connect(connection, &WatchConnection::watchConnected, this, &DevConnection::onWatchConnected);
    QObject::connect(connection, &WatchConnection::watchDisconnected, this, &DevConnection::onWatchDisconnected);
    QObject::connect(connection, &WatchConnection::rawIncomingMsg, this, &DevConnection::onRawIncomingMsg);
    QObject::connect(connection, &WatchConnection::rawOutgoingMsg, this, &DevConnection::onRawOutgoingMsg);
}
DevConnection::~DevConnection()
{
    disableConnection();
}

bool DevConnection::enabled() const
{
    return m_qtwsServer!=nullptr;
}
quint16 DevConnection::listenPort() const
{
    return m_port;
}
bool DevConnection::cloudEnabled() const
{
    return false; // TODO
}
// pebble slots
void DevConnection::onEnableChanged(bool enabled)
{
    if(enabled && m_qtwsServer==nullptr) {
        enableConnection(m_port);
    } else if(!enabled && m_qtwsServer) {
        disableConnection();
    }
}
void DevConnection::onPortChanged(quint16 port) {
    if(port != m_port && port > 0 && m_qtwsServer) {
        if(m_qtwsServer->isListening())
            disableConnection();
        enableConnection(port);
    } else {
        m_port = port;
    }
}
void DevConnection::onCloudEnableChanged(bool enabled)
{
    Q_UNUSED(enabled); // no idea how it works
    // Pebble-tool connects to wss://cloudpebble-ws-proxy-prod.herokuapp.com/tool
    // But what does phone do - need to capture with mitm (:TODO:)
}
void DevConnection::onWatchConnected()
{
    qDebug() << "Watch connected, need to resume DevCon in progress: " << m_clients.length();
}
void DevConnection::onWatchDisconnected()
{
    qDebug() << "Watch disconnected, need to pause DevCon in progress: " << m_clients.length();
}
void DevConnection::onRawIncomingMsg(const QByteArray &msg)
{
    QByteArray data=QByteArray(msg);
    data.prepend(char(0));
    broadcast(data);
    qDebug() << "Broadcast inMsg: " << data.toHex();
}
void DevConnection::onRawOutgoingMsg(const QByteArray &msg)
{
    QByteArray data=QByteArray(msg);
    data.prepend(char(1));
    broadcast(data);
    qDebug() << "Broadcast outMsg: " << data.toHex();
}
void DevConnection::onAppLogMsg(const QByteArray &msg)
{
    QByteArray data=QByteArray(QString(msg).toUtf8());
    data.prepend(char(2));
    broadcast(data);
    qDebug() << "Broadcast appLog: " << data.toHex();
}

// other api slots
void DevConnection::disableConnection()
{
    if(m_qtwsServer) {
        delete m_qtwsServer;
        m_qtwsServer = nullptr;
    }
    if(m_clients.length()>0) {
        QWebSocket *sock;
        foreach (sock, m_clients) {
            sock->close();
            delete sock;
        }
        m_clients.clear();
    }
}
void DevConnection::enableConnection(quint16 port)
{
    if(m_qtwsServer==nullptr) {
        m_qtwsServer=new QWebSocketServer("Jolla",QWebSocketServer::SslMode::NonSecureMode,this);
        QObject::connect(m_qtwsServer, &QWebSocketServer::newConnection, this, &DevConnection::socketConnected);
    }
    if(port>0) {
        m_port=port;
        if(m_qtwsServer->isListening())
            m_qtwsServer->close();
        if(m_qtwsServer->listen(QHostAddress::Any,m_port)) {
            qDebug() << "DevConnection listening on *:" << m_port;
        } else {
            qWarning() << "Error listening on " << port << ": " << m_qtwsServer->errorString();
            delete m_qtwsServer;
            m_qtwsServer = nullptr;
        }
    }
}
void DevConnection::sendToWatch(const QByteArray &msg)
{
    m_connection->writeRawData(msg);
}

// private slots/handlers
void DevConnection::socketConnected()
{
    QWebSocket *sock = m_qtwsServer->nextPendingConnection();
    m_clients.append(sock);
    qDebug() << "Accepted new connection from " << sock->peerAddress().toString();
    QObject::connect(sock,&QWebSocket::textMessageReceived,this,&DevConnection::textDataReceived);
    QObject::connect(sock,&QWebSocket::binaryMessageReceived,this,&DevConnection::rawDataReceived);
    QObject::connect(sock,&QWebSocket::disconnected,this,&DevConnection::socketDisconnected);
}
void DevConnection::socketDisconnected()
{
     QWebSocket *sock = qobject_cast<QWebSocket *>(sender());
     qDebug() << "Client disconnected: " << sock->peerAddress().toString();
     m_clients.removeAll(sock);
     delete sock;
}
void DevConnection::textDataReceived(QString msg)
{
    // We aren't supposed to receive sms are we? remove this sun-over-bij
     QWebSocket *sock = qobject_cast<QWebSocket *>(sender());
     sock->close();
     m_clients.removeAll(sock);
     delete sock;
     // what did he say anyway?
     qWarning() << "Unexpected Text Message received while waiting for ByteArray:" << msg;
}
void DevConnection::rawDataReceived(QByteArray data)
{
     QWebSocket *sock = qobject_cast<QWebSocket *>(sender());
     try {
        DevPacket *pkt = DevPacket::CreatePacket(data,sock,this);
        pkt->execute();
        if(!pkt->isValid()) {
            delete pkt;
        } else {
            QObject::connect(pkt,&DevPacket::complete,[pkt](){delete pkt;});
        }
     } catch(QException &e) {
         qWarning() << "DevPacket not understood: " << data;
     }
}
void DevConnection::broadcast(const QByteArray &msg)
{
    if(m_clients.length()>0) {
        QWebSocket *sock;
        foreach (sock, m_clients) {
            if(sock->sendBinaryMessage(msg)!=msg.size()) {
                qWarning() << "Failure while sending to " << sock->peerAddress().toString();
                sock->close();
                m_clients.removeAll(sock);
                delete sock;
            }
        }
    }
}

// DevPacket implementation (and definition)
DevPacket::DevPacket(QByteArray &data, QWebSocket *sock, DevConnection *srv):
    QObject(srv),
    m_data(data),
    m_sock(nullptr),
    m_srv(srv)
{
    QObject::connect(sock,&QWebSocket::disconnected,this,&DevPacket::complete);
    //QObject::connect(sock,&QWebSocket::destroyed,this,&DevPacket::complete);
}
void DevPacket::serverDown()
{
    emit complete();
}

class DevPacketRelay:public DevPacket
{
public:
    DevPacketRelay(QByteArray &data, QWebSocket *sock, DevConnection *srv):
        DevPacket(data,sock,srv)
    {
        m_sock=sock;
    }

    bool isRequest() const { return true;}
    bool isReply() const { return true;}
    void execute() {
        qDebug() << "Relay this (" << m_data.toHex() << ") to pebble";
        m_srv->sendToWatch(m_data.right(m_data.size()-1));
    }
};
class DevPacketPInfo:public DevPacket
{
public:
    DevPacketPInfo(QByteArray &data, QWebSocket *sock, DevConnection *srv):
        DevPacket(data,sock,srv)
    {
        m_sock=sock;
        m_data.replace(1,m_data.size(),"rocpoold,1.0.0,pebble");
    }

    bool isRequest() const {return true;}
    bool isReply() const {return true;}
    void execute() {m_sock->sendBinaryMessage(m_data);m_sock=nullptr;}
};

DevPacket * DevPacket::CreatePacket(QByteArray &data, QWebSocket *sock, DevConnection *srv)
{
    char opcode = data.at(0);
    switch (opcode) {
    case OCPhoneServerInfo:
        return new DevPacketPInfo(data,sock,srv);
        break;
    case OCRelayToWatch:
        return new DevPacketRelay(data,sock,srv);
        break;
    default:
        throw QException();
    }
    return nullptr;
}