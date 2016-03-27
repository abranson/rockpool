#include <QBuffer>
#include <QAuthenticator>
#include <QEventLoop>

#include "jskitxmlhttprequest.h"
#include "jskitmanager.h"

JSKitXMLHttpRequest::JSKitXMLHttpRequest(QJSEngine *engine) :
    QObject(engine),
    l(metaObject()->className()),
    m_engine(engine),
    m_net(new QNetworkAccessManager(this)),
    m_timeout(0),
    m_reply(0)
{
    connect(m_net, &QNetworkAccessManager::authenticationRequired,
            this, &JSKitXMLHttpRequest::handleAuthenticationRequired);
}

void JSKitXMLHttpRequest::open(const QString &method, const QString &url, bool async, const QString &username, const QString &password)
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = 0;
    }

    m_username = username;
    m_password = password;
    m_request = QNetworkRequest(QUrl(url));
    m_verb = method;
    m_async = async;

    qCDebug(l) << "opened to URL" << m_request.url().toString() << "Async:" << async;
}

void JSKitXMLHttpRequest::setRequestHeader(const QString &header, const QString &value)
{
    qCDebug(l) << "setRequestHeader" << header << value;
    m_request.setRawHeader(header.toLatin1(), value.toLatin1());
}

void JSKitXMLHttpRequest::send(const QJSValue &data)
{
    QByteArray byteData;

    if (data.isUndefined() || data.isNull()) {
        // Do nothing, byteData is empty.
    } else if (data.isString()) {
        byteData = data.toString().toUtf8();
    } else if (data.isObject()) {
        if (data.hasProperty("byteLength")) {
            // Looks like an ArrayView or an ArrayBufferView!
            QJSValue buffer = data.property("buffer");
            if (buffer.isUndefined()) {
                // We must assume we've been passed an ArrayBuffer directly
                buffer = data;
            }

            QJSValue array = data.property("_bytes");
            int byteLength = data.property("byteLength").toInt();

            if (array.isArray()) {
                byteData.reserve(byteLength);

                for (int i = 0; i < byteLength; i++) {
                    byteData.append(array.property(i).toInt());
                }

                qCDebug(l) << "passed an ArrayBufferView of" << byteData.length() << "bytes";
            } else {
                qCWarning(l) << "passed an unknown/invalid ArrayBuffer" << data.toString();
            }
        } else {
            qCWarning(l) << "passed an unknown object" << data.toString();
        }

    }

    QBuffer *buffer;
    if (!byteData.isEmpty()) {
        buffer = new QBuffer;
        buffer->setData(byteData);
    } else {
        buffer = 0;
    }

    qCDebug(l) << "sending" << m_verb << "to" << m_request.url() << "with" << QString::fromUtf8(byteData);
    m_reply = m_net->sendCustomRequest(m_request, m_verb.toLatin1(), buffer);

    connect(m_reply, &QNetworkReply::finished,
            this, &JSKitXMLHttpRequest::handleReplyFinished);
    connect(m_reply, static_cast<void (QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this, &JSKitXMLHttpRequest::handleReplyError);

    if (buffer) {
        // So that it gets deleted alongside the reply object.
        buffer->setParent(m_reply);
    }

    if (!m_async) {
        QEventLoop loop; //Hacky way to get QNetworkReply be synchronous

        connect(m_reply, &QNetworkReply::finished,
                &loop, &QEventLoop::quit);
        connect(m_reply, static_cast<void (QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
                &loop, &QEventLoop::quit);

        loop.exec();
    }
}

void JSKitXMLHttpRequest::abort()
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = 0;
    }
}

QJSValue JSKitXMLHttpRequest::onload() const
{
    return m_onload;
}

void JSKitXMLHttpRequest::setOnload(const QJSValue &value)
{
    m_onload = value;
}

QJSValue JSKitXMLHttpRequest::onreadystatechange() const
{
    return m_onreadystatechange;
}

void JSKitXMLHttpRequest::setOnreadystatechange(const QJSValue &value)
{
    m_onreadystatechange = value;
}

QJSValue JSKitXMLHttpRequest::ontimeout() const
{
    return m_ontimeout;
}

void JSKitXMLHttpRequest::setOntimeout(const QJSValue &value)
{
    m_ontimeout = value;
}

QJSValue JSKitXMLHttpRequest::onerror() const
{
    return m_onerror;
}

void JSKitXMLHttpRequest::setOnerror(const QJSValue &value)
{
    m_onerror = value;
}

uint JSKitXMLHttpRequest::readyState() const
{
    if (!m_reply) {
        return UNSENT;
    } else if (m_reply->isFinished()) {
        return DONE;
    } else {
        return LOADING;
    }
}

uint JSKitXMLHttpRequest::timeout() const
{
    return m_timeout;
}

void JSKitXMLHttpRequest::setTimeout(uint value)
{
    m_timeout = value;
    // TODO Handle fetch in-progress.
}

uint JSKitXMLHttpRequest::status() const
{
    if (!m_reply || !m_reply->isFinished()) {
        return 0;
    } else {
        return m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toUInt();
    }
}

QString JSKitXMLHttpRequest::statusText() const
{
    if (!m_reply || !m_reply->isFinished()) {
        return QString();
    } else {
        return m_reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }
}

QString JSKitXMLHttpRequest::responseType() const
{
    return m_responseType;
}

void JSKitXMLHttpRequest::setResponseType(const QString &type)
{
    qCDebug(l) << "response type set to" << type;
    m_responseType = type;
}

void JSKitXMLHttpRequest::addEventListener(const QString &type, QJSValue function)
{
    m_listeners[type].append(function);
}

void JSKitXMLHttpRequest::removeEventListener(const QString &type, QJSValue function)
{
    if (!m_listeners.contains(type)) return;

    QList<QJSValue> &callbacks = m_listeners[type];
    for (QList<QJSValue>::iterator it = callbacks.begin(); it != callbacks.end(); ) {
        if (it->strictlyEquals(function)) {
            it = callbacks.erase(it);
        } else {
            ++it;
        }
    }

    if (callbacks.empty()) {
        m_listeners.remove(type);
    }
}

void JSKitXMLHttpRequest::invokeCallbacks(const QString &type, const QJSValueList &args)
{
    if (!m_listeners.contains(type)) return;
    QList<QJSValue> &callbacks = m_listeners[type];

    for (QList<QJSValue>::iterator it = callbacks.begin(); it != callbacks.end(); ++it) {
        qCDebug(l) << "invoking callback" << type << it->toString();
        QJSValue result = it->call(args);
        if (result.isError()) {
            qCWarning(l) << "error while invoking callback"
                << type << it->toString() << ":"
                << JSKitManager::describeError(result);
        }
    }
}

QJSValue JSKitXMLHttpRequest::response() const
{
    if (m_responseType.isEmpty() || m_responseType == "text") {
        return m_engine->toScriptValue(QString::fromUtf8(m_response));
    } else if (m_responseType == "arraybuffer") {
        QJSValue arrayBufferProto = m_engine->globalObject().property("ArrayBuffer").property("prototype");
        QJSValue arrayBuf = m_engine->newObject();

        if (!arrayBufferProto.isUndefined()) {
            arrayBuf.setPrototype(arrayBufferProto);
            arrayBuf.setProperty("byteLength", m_engine->toScriptValue<uint>(m_response.size()));

            QJSValue array = m_engine->newArray(m_response.size());
            for (int i = 0; i < m_response.size(); i++) {
                array.setProperty(i, m_engine->toScriptValue<int>(m_response[i]));
            }

            arrayBuf.setProperty("_bytes", array);
            qCDebug(l) << "returning ArrayBuffer of" << m_response.size() << "bytes";
        } else {
            qCWarning(l) << "Cannot find proto of ArrayBuffer";
        }

        return arrayBuf;
    } else {
        qCWarning(l) << "unsupported responseType:" << m_responseType;
        return m_engine->toScriptValue<void*>(0);
    }
}

QString JSKitXMLHttpRequest::responseText() const
{
    return QString::fromUtf8(m_response);
}

void JSKitXMLHttpRequest::handleReplyFinished()
{
    if (!m_reply) {
        qCDebug(l) << "reply finished too late";
        return;
    }

    m_response = m_reply->readAll();
    qCDebug(l) << "reply finished, reply text:" << QString::fromUtf8(m_response) << "status:" << status();

    emit readyStateChanged();
    emit statusChanged();
    emit statusTextChanged();
    emit responseChanged();
    emit responseTextChanged();

    if (m_onload.isCallable()) {
        qCDebug(l) << "going to call onload handler:" << m_onload.toString();

        QJSValue result = m_onload.callWithInstance(m_engine->newQObject(this));
        if (result.isError()) {
            qCWarning(l) << "JS error on onload handler:" << JSKitManager::describeError(result);
        }
    } else {
        qCDebug(l) << "No onload set";
    }
    invokeCallbacks("load", QJSValueList({m_engine->newQObject(this)}));

    if (m_onreadystatechange.isCallable()) {
        qCDebug(l) << "going to call onreadystatechange handler:" << m_onreadystatechange.toString();
        QJSValue result = m_onreadystatechange.callWithInstance(m_engine->newQObject(this));
        if (result.isError()) {
            qCWarning(l) << "JS error on onreadystatechange handler:" << JSKitManager::describeError(result);
        }
    }
    invokeCallbacks("readystatechange", QJSValueList({m_engine->newQObject(this)}));
}

void JSKitXMLHttpRequest::handleReplyError(QNetworkReply::NetworkError code)
{
    if (!m_reply) {
        qCDebug(l) << "reply error too late";
        return;
    }

    qCDebug(l) << "reply error" << code;

    emit readyStateChanged();
    emit statusChanged();
    emit statusTextChanged();

    if (m_onerror.isCallable()) {
        qCDebug(l) << "going to call onerror handler:" << m_onload.toString();
        QJSValue result = m_onerror.callWithInstance(m_engine->newQObject(this));
        if (result.isError()) {
            qCWarning(l) << "JS error on onerror handler:" << JSKitManager::describeError(result);
        }
    }
    invokeCallbacks("error", QJSValueList({m_engine->newQObject(this)}));
}

void JSKitXMLHttpRequest::handleAuthenticationRequired(QNetworkReply *reply, QAuthenticator *auth)
{
    if (m_reply == reply) {
        qCDebug(l) << "authentication required";

        if (!m_username.isEmpty() || !m_password.isEmpty()) {
            qCDebug(l) << "using provided authorization:" << m_username;

            auth->setUser(m_username);
            auth->setPassword(m_password);
        } else {
            qCDebug(l) << "no username or password provided";
        }
    }
}
