#ifndef JSKITXMLHTTPREQUEST_P_H
#define JSKITXMLHTTPREQUEST_P_H

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJSEngine>
#include <QLoggingCategory>

class JSKitXMLHttpRequest : public QObject
{
    Q_OBJECT
    QLoggingCategory l;

    Q_PROPERTY(QJSValue onload READ onload WRITE setOnload)
    Q_PROPERTY(QJSValue onreadystatechange READ onreadystatechange WRITE setOnreadystatechange)
    Q_PROPERTY(QJSValue ontimeout READ ontimeout WRITE setOntimeout)
    Q_PROPERTY(QJSValue onerror READ onerror WRITE setOnerror)
    Q_PROPERTY(uint readyState READ readyState NOTIFY readyStateChanged)
    Q_PROPERTY(uint timeout READ timeout WRITE setTimeout)
    Q_PROPERTY(uint status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString responseType READ responseType WRITE setResponseType)
    Q_PROPERTY(QJSValue response READ response NOTIFY responseChanged)
    Q_PROPERTY(QString responseText READ responseText NOTIFY responseTextChanged)

public:
    explicit JSKitXMLHttpRequest(QJSEngine *engine);

    enum ReadyStates {
        UNSENT = 0,
        OPENED = 1,
        HEADERS_RECEIVED = 2,
        LOADING = 3,
        DONE = 4
    };
    Q_ENUMS(ReadyStates)

    Q_INVOKABLE void open(const QString &method, const QString &url, bool async = true, const QString &username = QString(), const QString &password = QString());
    Q_INVOKABLE void setRequestHeader(const QString &header, const QString &value);
    Q_INVOKABLE void send(const QJSValue &data = QJSValue(QJSValue::NullValue));
    Q_INVOKABLE void abort();

    Q_INVOKABLE void addEventListener(const QString &type, QJSValue function);
    Q_INVOKABLE void removeEventListener(const QString &type, QJSValue function);
    void invokeCallbacks(const QString &type, const QJSValueList &args = QJSValueList());
    QJSValue onload() const;
    void setOnload(const QJSValue &value);
    QJSValue onreadystatechange() const;
    void setOnreadystatechange(const QJSValue &value);
    QJSValue ontimeout() const;
    void setOntimeout(const QJSValue &value);
    QJSValue onerror() const;
    void setOnerror(const QJSValue &value);

    uint readyState() const;

    uint timeout() const;
    void setTimeout(uint value);

    uint status() const;
    QString statusText() const;

    QString responseType() const;
    void setResponseType(const QString& type);

    QJSValue response() const;
    QString responseText() const;

signals:
    void readyStateChanged();
    void statusChanged();
    void statusTextChanged();
    void responseChanged();
    void responseTextChanged();

private slots:
    void handleReplyFinished();
    void handleReplyError(QNetworkReply::NetworkError code);
    void handleAuthenticationRequired(QNetworkReply *reply, QAuthenticator *auth);

private:
    QJSEngine *m_engine;
    QNetworkAccessManager *m_net;
    QString m_verb;
    bool m_async = true;
    uint m_timeout;
    QString m_username;
    QString m_password;
    QNetworkRequest m_request;
    QNetworkReply *m_reply;
    QString m_responseType;
    QByteArray m_response;
    QHash<QString, QList<QJSValue>> m_listeners;
    QJSValue m_onload;
    QJSValue m_onreadystatechange;
    QJSValue m_ontimeout;
    QJSValue m_onerror;
};

#endif // JSKITXMLHTTPREQUEST_P_H
