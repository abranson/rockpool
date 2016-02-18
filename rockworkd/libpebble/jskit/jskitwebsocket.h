#ifndef JSKITWEBSOCKET_P_H
#define JSKITWEBSOCKET_P_H

#include <QLoggingCategory>
#include <QWebSocket>
#include <QJSEngine>

class JSKitWebSocket : public QObject
{
    Q_OBJECT
    QLoggingCategory l;

    Q_PROPERTY(QString binaryType MEMBER m_binaryType)
    Q_PROPERTY(quint32 bufferedAmount READ bufferedAmount)
    Q_PROPERTY(QString extensions MEMBER m_extensions)
    Q_PROPERTY(QJSValue onclose READ onclose WRITE setOnclose)
    Q_PROPERTY(QJSValue onerror READ onerror WRITE setOnerror)
    Q_PROPERTY(QJSValue onmessage READ onmessage WRITE setOnmessage)
    Q_PROPERTY(QJSValue onopen READ onopen WRITE setOnopen)
    Q_PROPERTY(QString protocol MEMBER m_protocol)
    Q_PROPERTY(quint8 readyState READ readyState)
    Q_PROPERTY(QString url READ url)

public:
    explicit JSKitWebSocket(QJSEngine *engine, const QString &url, const QJSValue &protocols=QJSValue());

    enum ReadyStates {
        CONNECTING = 0,
        OPEN = 1,
        CLOSING = 2,
        CLOSED = 3
    };
    Q_ENUMS(ReadyStates)

    Q_INVOKABLE void send(const QJSValue &data);
    Q_INVOKABLE void close(quint32 code=1000, const QString &reason=QString());

    void setOnclose(const QJSValue &onclose);
    void setOnerror(const QJSValue &onerror);
    void setOnmessage(const QJSValue &onmessage);
    void setOnopen(const QJSValue &onopen);

    QJSValue onclose() const;
    QJSValue onerror() const;
    QJSValue onmessage() const;
    QJSValue onopen() const;

    quint32 bufferedAmount();
    quint8 readyState();
    QString url();

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleError(QAbstractSocket::SocketError error);
    void handleSslErrors(const QList<QSslError> &errors);
    void handleTextMessageReceived(const QString &message);
    void handleBinaryMessageReceived(const QByteArray &message);

private:
    void callOnmessage(QJSValue data);

private:
    QJSEngine *m_engine;
    QWebSocket *m_webSocket;

    QString m_binaryType = "arraybuffer";
    quint32 m_bufferedAmount = 0;
    QString m_extensions;
    QJSValue m_onclose;
    QJSValue m_onerror;
    QJSValue m_onmessage;
    QJSValue m_onopen;
    QString m_protocol;
    quint8 m_readyState = CONNECTING;
    QString m_url;
};

#endif // JSKITWEBSOCKET_P_H
