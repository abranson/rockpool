#ifndef DEVCONNECTION_H
#define DEVCONNECTION_H

#include <QObject>

QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
QT_FORWARD_DECLARE_CLASS(QWebSocket)
QT_FORWARD_DECLARE_CLASS(DevPacket)
QT_FORWARD_DECLARE_CLASS(Pebble)
QT_FORWARD_DECLARE_CLASS(WatchConnection)

class DevConnection : public QObject
{
    Q_OBJECT
public:
    explicit DevConnection(Pebble *pebble, WatchConnection *connection);
    virtual ~DevConnection();

    bool enabled() const;
    bool cloudEnabled() const;
    quint16 listenPort() const;

    bool serverState() const;
    bool cloudState() const;

signals:
    void serverStateChanged(bool state);
    void cloudStateChanged(bool state);
    void insertPin(const QJsonObject &pin);
    void removePin(const QString &guid);

public slots:
    void enableConnection(quint16 port);
    void disableConnection();

    void setEnabled(bool enabled);
    void setPort(quint16 port);
    void setCloudEnabled(bool enabled);

    void onWatchConnected();
    void sendToWatch(const QByteArray &msg);
    void installBundle(const QString &file);
    void onWatchDisconnected();
    void onRawIncomingMsg(const QByteArray &msg);
    void onRawOutgoingMsg(const QByteArray &msg);
private slots:
    void socketConnected();
    void socketDisconnected();
    void textDataReceived(const QString &msg);
    void rawDataReceived(const QByteArray &data);
    void handleMessage(const QByteArray &data);
    void broadcast(const QByteArray &msg);
private:
    QList<QWebSocket *> m_clients;
    Pebble *m_pebble;
    WatchConnection *m_connection;
    QWebSocketServer *m_qtwsServer;
    quint16 m_port = 0;
    // kinda singleton
    static DevConnection *s_instance;
    static void appLogBroadcast(QtMsgType t, const QMessageLogContext &ctx, const QString &msg);
    static QtMessageHandler s_omh;
};

class DevPacket : public QObject
{
    Q_OBJECT
public:
    enum OpCode {
        OCRelayFromWatch,       // 0 <-bytes
        OCRelayToWatch,         // 1 ->bytes
        OCPhoneAppLog,          // 2 <-
        OCPhoneServerLog,       // 3 <-
        OCInstallBundle,        // 4 ->bytes
        OCInstallStatus,        // 5 <-{uint32(1),uint32(0)}
        OCPhoneServerInfo,      // 6 <-strz
        OCConStatusUpdate,      // 7
        OCProxyStatusUpdate,    // 8 {0,ff}
        OCProxyAuthResponse,    // 9 {0,1}
        OCPhoneConfigResponse,  // a
        OCRelayEmulator,        // b
        OCTimelineOperation     // c <>{char(0x0|0x1|0x2),"json"}
    };

    bool isValid() {return m_sock != nullptr;}
    virtual bool isRequest() const = 0;
    virtual bool isReply() const = 0;
    virtual void execute() = 0;
    static DevPacket * CreatePacket(const QByteArray &data, QWebSocket *sock, DevConnection *srv);
    virtual ~DevPacket() = default;
signals:
    void complete();

private slots:
    void serverDown();

protected:
    DevPacket(const QByteArray &data, QWebSocket *sock, DevConnection *srv);
    QByteArray m_data;
    QWebSocket *m_sock;
    DevConnection *m_srv;
};

#endif // DEVCONNECTION_H
