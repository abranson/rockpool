#include "devconnection.h"

#include "pebble.h"
#include "watchconnection.h"
#include "watchdatareader.h"

#include <QException>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <QMutex>
#include <QDebug>

/// App logging
// Statics init
DevConnection * DevConnection::s_instance=0;
QtMessageHandler DevConnection::s_omh=0;
QFile * DevConnection::s_dump = nullptr;
int DevConnection::s_logSev = 1;
//
QMutex mtx;
void DevConnection::installLogging(DevConnection *instance, bool override)
{
    if(s_instance != nullptr && !override) return;
    s_instance = instance;
    if(s_omh == 0) {
        qDebug() << "Acquiring logging channel, use DevConnection to manage verbosity";
        s_omh = qInstallMessageHandler(appLogBroadcast);
    }
}
void DevConnection::destroyLogging(DevConnection *instance)
{
    if(s_instance==instance) {
        s_instance = nullptr;
        if(s_omh) {
            mtx.lock();
            qInstallMessageHandler(s_omh);
            s_omh = 0;
            mtx.unlock();
            qDebug() << "Turned off custom log handler";
        }
    }
}

QStringList ml={"D","W","C","F"};
void DevConnection::appLogBroadcast(QtMsgType t, const QMessageLogContext &ctx, const QString &msg)
{
    if(s_omh && t >= s_logSev)
        s_omh(t,ctx,msg);
    mtx.lock();
    if(s_instance) {
        QByteArray m(1,char(DevPacket::OCPhoneAppLog));
        m.append(QString("[%1] %2:%3 %4").arg(ml.at(t)).arg(ctx.function).arg(ctx.line).arg(msg).toUtf8());
        QMetaObject::invokeMethod(s_instance,"broadcast",Qt::QueuedConnection,Q_ARG(QByteArray,m));
    }
    mtx.unlock();
    if(s_dump && s_dump->isOpen()) {
        s_dump->write(QString("%1 [%2] %3:%4 %5\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate),ml.at(t),ctx.function,QString::number(ctx.line),msg).toUtf8());
    }
}
void DevConnection::setLogLevel(int level)
{
    s_logSev = level;
}
int DevConnection::getLogLevel()
{
    return s_logSev;
}

QString DevConnection::startLogDump()
{
    if(s_dump)
        delete s_dump;
    s_dump = new QTemporaryFile("/tmp/rockpoold.XXXXXX.log");
    if(s_dump->open(QFile::ReadWrite))
        return s_dump->fileName();
    delete s_dump;
    s_dump = nullptr;
    return "";
}
QString DevConnection::stopLogDump()
{
    if(s_dump && s_dump->isOpen()) {
        s_dump->close();
        return s_dump->fileName();
    }
    return "";
}
QString DevConnection::getLogDump()
{
    return (s_dump)?s_dump->fileName():"";
}
bool DevConnection::isLogDumping()
{
    return (s_dump && s_dump->isOpen());
}

DevConnection::DevConnection(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection),
    m_qtwsServer(0)
{
    //connect(connection, &WatchConnection::watchConnected, this, &DevConnection::onWatchConnected);
    //connect(connection, &WatchConnection::watchDisconnected, this, &DevConnection::onWatchDisconnected);
    connection->registerEndpointHandler(WatchConnection::EndpointAppLogs, this, "handleMessage");
    connect(connection, &WatchConnection::rawIncomingMsg, this, &DevConnection::onRawIncomingMsg);
    connect(connection, &WatchConnection::rawOutgoingMsg, this, &DevConnection::onRawOutgoingMsg);
    connect(this, &DevConnection::insertPin, m_pebble, &Pebble::insertPin);
    connect(this, &DevConnection::removePin, m_pebble, &Pebble::removePin);
    installLogging(this);
}
DevConnection::~DevConnection()
{
    disableConnection();
    destroyLogging(this);

}

void DevConnection::handleMessage(const QByteArray &data)
{
    if(!m_clients.isEmpty()) return;
    WatchDataReader reader(data);
    QUuid uuid = reader.readUuid();
    QDateTime ts = QDateTime::fromTime_t(reader.read<quint32>(),Qt::UTC);
    quint8 lvl = reader.read<quint8>();
    quint8 len = reader.read<quint8>();
    quint16 lno = reader.read<quint16>();
    QString fnm = reader.readFixedString(16);
    QString msg = reader.readFixedString(len);
    qDebug() << QString("%1 <%2> %3: %4:%5 %6").arg(ts.toString(Qt::ISODate),QString::number(lvl),uuid.toString().mid(1,36),fnm,QString::number(lno),msg);
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
bool DevConnection::serverState() const
{
    return m_qtwsServer && m_qtwsServer->isListening();
}
bool DevConnection::cloudState() const
{
    return false; // TODO
}
// pebble slots
void DevConnection::setEnabled(bool enabled)
{
    if(enabled && m_qtwsServer==nullptr) {
        enableConnection(m_port);
    } else if(!enabled && m_qtwsServer) {
        disableConnection();
    }
}
void DevConnection::setPort(quint16 port) {
    if(port != m_port && port > 0 && m_qtwsServer) {
        if(m_qtwsServer->isListening())
            disableConnection();
        enableConnection(port);
    } else {
        m_port = port;
    }
}
void DevConnection::setCloudEnabled(bool enabled)
{
    // no idea how it works
    qDebug() << "Request to" << (enabled?"enable":"disable") << "cloud connection";
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
}
void DevConnection::onRawOutgoingMsg(const QByteArray &msg)
{
    QByteArray data=QByteArray(msg);
    data.prepend(char(1));
    broadcast(data);
}

// other api slots
void DevConnection::disableConnection()
{
    if(m_qtwsServer) {
        m_qtwsServer->close();
        m_qtwsServer->deleteLater();
        m_qtwsServer = nullptr;
        emit serverStateChanged(false);
    }
    if(m_clients.length()>0) {
        QWebSocket *sock;
        foreach (sock, m_clients) {
            sock->close();
            sock->deleteLater();
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
        if(m_qtwsServer->isListening()) {
            m_qtwsServer->close();
            emit serverStateChanged(false);
        }
        if(m_qtwsServer->listen(QHostAddress::Any,m_port)) {
            qDebug() << "DevConnection listening on *:" << m_port;
            emit serverStateChanged(true);
        } else {
            qWarning() << "Error listening on" << port << ": " << m_qtwsServer->errorString();
            delete m_qtwsServer;
            m_qtwsServer = nullptr;
        }
    }
}
void DevConnection::sendToWatch(const QByteArray &msg)
{
    m_connection->writeRawData(msg);
}
void DevConnection::installBundle(const QString &file)
{
    m_pebble->sideloadApp(file);
}

// private slots/handlers
void DevConnection::socketConnected()
{
    QWebSocket *sock = m_qtwsServer->nextPendingConnection();
    m_clients.append(sock);
    qDebug() << "Accepted new connection from" << sock->peerAddress().toString();
    QObject::connect(sock,&QWebSocket::textMessageReceived,this,&DevConnection::textDataReceived);
    QObject::connect(sock,&QWebSocket::binaryMessageReceived,this,&DevConnection::rawDataReceived);
    QObject::connect(sock,&QWebSocket::disconnected,this,&DevConnection::socketDisconnected);
}
void DevConnection::socketDisconnected()
{
     QWebSocket *sock = qobject_cast<QWebSocket *>(sender());
     m_clients.removeAll(sock);
     sock->deleteLater();
     qDebug() << "Client disconnected:" << sock->peerAddress().toString();
}
void DevConnection::textDataReceived(const QString &msg)
{
    // We aren't supposed to receive sms are we? remove this sun-over-bij
     QWebSocket *sock = qobject_cast<QWebSocket *>(sender());
     sock->close();
     // what did he say anyway?
     qWarning() << "Unexpected Text Message received while waiting for ByteArray:" << msg;
}
void DevConnection::rawDataReceived(const QByteArray &data)
{
     QWebSocket *sock = qobject_cast<QWebSocket *>(sender());
     try {
        DevPacket *pkt = DevPacket::CreatePacket(data,sock,this);
        pkt->execute();
        if(!pkt->isValid()) {
            pkt->deleteLater();
            qDebug() << "Packet has no pending tasks, deleting";
        }
     } catch(QException &e) {
         qWarning() << "DevPacket not understood:" << data;
     }
}
void DevConnection::broadcast(const QByteArray &msg)
{
    if(m_clients.length()>0) {
        QWebSocket *sock;
        foreach (sock, m_clients) {
            sock->sendBinaryMessage(msg);
        }
#ifdef DEVCON_VERBOSE
        if(msg.at(0)<2)
            qDebug() << "Broadcast" << (msg.at(0)==1?"outMsg":"inMsg") << msg.toHex();
#endif
    }
}

// DevPacket implementation (and definition)
DevPacket::DevPacket(const QByteArray &data, QWebSocket *sock, DevConnection *srv):
    QObject(srv),
    m_data(data),
    m_sock(nullptr),
    m_srv(srv)
{
    QObject::connect(sock,&QWebSocket::disconnected,this,&DevPacket::complete);
    QObject::connect(this,&DevPacket::complete,this,&DevPacket::deleteLater);
}
void DevPacket::serverDown()
{
    emit complete();
}

class DevPacketRelay:public DevPacket
{
public:
    DevPacketRelay(const QByteArray &data, QWebSocket *sock, DevConnection *srv):
        DevPacket(data,sock,srv)
    {
        qDebug() << "Relay this (" << m_data.toHex() << ") to pebble";
    }
    bool isRequest() const { return true;}
    bool isReply() const { return true;}
    void execute() {
        m_srv->sendToWatch(m_data.right(m_data.size()-1));
    }
};
class DevPacketPInfo:public DevPacket
{
public:
    DevPacketPInfo(const QByteArray &data, QWebSocket *sock, DevConnection *srv):
        DevPacket(data,sock,srv)
    {
        m_sock=sock;
        m_data.replace(1,m_data.size(),"rocpoold,1.5.0,pebble");
    }

    bool isRequest() const {return true;}
    bool isReply() const {return true;}
    void execute() {m_sock->sendBinaryMessage(m_data);m_sock=nullptr;}
};
class DevPacketInstall: public DevPacket
{
public:
    DevPacketInstall(const QByteArray &data, QWebSocket *sock, DevConnection *srv):
        DevPacket(data,sock,srv)
    {
        m_sock = sock;
        m_data.remove(0,1);
    }
    bool isRequest() const {return true;}
    bool isReply() const {return false;}
    void execute() {
        QTemporaryFile f;
        if(f.open()) {
            f.write(m_data);
            f.flush();
            f.close();
            qDebug() << "Installing bundle from file" << f.fileName();
            m_srv->installBundle(f.fileName());
            m_data.fill('\0',4);
            m_data.prepend('\5');
            m_sock->sendBinaryMessage(m_data);
            m_sock=nullptr;
        }
    }
};
class DevPacketTimeline: public DevPacket
{
public:
    DevPacketTimeline(const QByteArray &data, QWebSocket *sock, DevConnection *srv):
        DevPacket(data,sock,srv)
    {
        m_sock=sock;
        m_cmd = m_data.at(1);
        m_data.remove(0,2);
        QJsonParseError jpe;
        m_json=QJsonDocument::fromJson(m_data,&jpe);
        qDebug() << "Got packet with payload" << m_data << jpe.errorString();
        m_data.truncate(0);
        m_data.append(char(OCTimelineOperation));
    }
    bool isRequest() const { return true;}
    bool isReply() const { return true;}
    void execute() {
        if(m_cmd==1) {
            qDebug() << "Add pin" << m_json.toJson(QJsonDocument::JsonFormat::Indented);
            emit m_srv->insertPin(m_json.object());
        } else if(m_cmd==2) {
            emit m_srv->removePin(m_json.object().value("guid").toString());
            qDebug() << "Del pin" << m_json.toJson(QJsonDocument::JsonFormat::Indented);
        }
        m_data.append(char(0));
        m_sock->sendBinaryMessage(m_data);
        m_sock=nullptr;
    }
private:
    char m_cmd;
    QJsonDocument m_json;
};

DevPacket * DevPacket::CreatePacket(const QByteArray &data, QWebSocket *sock, DevConnection *srv)
{
    char opcode = data.at(0);
    switch (opcode) {
    case OCPhoneServerInfo:
        return new DevPacketPInfo(data,sock,srv);
    case OCRelayToWatch:
        return new DevPacketRelay(data,sock,srv);
    case OCInstallBundle:
        return new DevPacketInstall(data,sock,srv);
    case OCTimelineOperation:
        return new DevPacketTimeline(data,sock,srv);
    default:
        throw QException();
    }
    return nullptr;
}
