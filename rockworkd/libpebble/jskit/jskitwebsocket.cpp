#include "jskitwebsocket.h"
#include "jskitmanager.h"

JSKitWebSocket::JSKitWebSocket(QJSEngine *engine, const QString &url, const QJSValue &protocols) :
    QObject(engine),
    l(metaObject()->className()),
    m_engine(engine),
    m_webSocket(new QWebSocket("", QWebSocketProtocol::VersionLatest, this)),
    m_url(url)
{
    //As of QT 5.5: "QWebSocket currently does not support extensions and subprotocols"
    Q_UNUSED(protocols)

    connect(m_webSocket, &QWebSocket::connected,
            this, &JSKitWebSocket::handleConnected);
    connect(m_webSocket, &QWebSocket::disconnected,
            this, &JSKitWebSocket::handleDisconnected);
    connect(m_webSocket, static_cast<void(QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error),
            this, &JSKitWebSocket::handleError);
    connect(m_webSocket, &QWebSocket::sslErrors,
            this, &JSKitWebSocket::handleSslErrors);
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &JSKitWebSocket::handleTextMessageReceived);
    connect(m_webSocket, &QWebSocket::binaryMessageReceived,
            this, &JSKitWebSocket::handleBinaryMessageReceived);

    qCDebug(l) << "WebSocket opened for" << url;
    //m_webSocket->ignoreSslErrors();
    m_webSocket->open(QUrl(url));
}

void JSKitWebSocket::send(const QJSValue &data)
{
    //TODO throw SYNTAX_ERR if "The data is a string that has unpaired surrogates" - https://developer.mozilla.org/en-US/docs/Web/API/WebSocket#Exceptions_thrown_2

    if (m_readyState != OPEN) {
        //TODO throw INVALID_STATE_ERR if not opened - https://developer.mozilla.org/en-US/docs/Web/API/WebSocket#Exceptions_thrown_2
        qCDebug(l) << "trying to send when connection is not yet open";

        return;
    }

    if (data.isUndefined() || data.isNull()) {
        qCDebug(l) << "Refusing to send a null or undefined message";
    } else if (data.isString()) {
        qCDebug(l) << "Sending text message:" << data.toString();

        QByteArray byteData = data.toString().toUtf8();
        m_bufferedAmount += byteData.size();

        m_webSocket->sendTextMessage(data.toString());
    } else if (data.isObject()) {
        if (data.hasProperty("byteLength")) {
            // Looks like an ArrayView or an ArrayBufferView!
            QJSValue buffer = data.property("buffer");
            if (buffer.isUndefined()) {
                // We must assume we've been passed an ArrayBuffer directly
                buffer = data;
            }

            QJSValue array = buffer.property("_bytes");
            int byteLength = buffer.property("byteLength").toInt();

            if (array.isArray()) {
                QByteArray byteData;
                byteData.reserve(byteLength);

                for (int i = 0; i < byteLength; i++) {
                    byteData.append(array.property(i).toInt());
                }

                qCDebug(l) << "sending binary message with" << byteData.length() << "bytes";

                m_bufferedAmount += byteData.size();
                m_webSocket->sendBinaryMessage(byteData);
            } else {
                qCWarning(l) << "Refusing to send an unknown/invalid ArrayBuffer" << data.toString();
            }
        } else {
            qCWarning(l) << "Refusing to send an unknown object:" << data.toString();
        }
    }
}

void JSKitWebSocket::close(quint32 code, const QString &reason)
{
    //TODO throw SYNTAX_ERR if "The reason string contains unpaired surrogates" - https://developer.mozilla.org/en-US/docs/Web/API/WebSocket#Exceptions_thrown_2


    QByteArray byteData = reason.toUtf8();
    if (byteData.size() >= 123) {
        //TODO throw SYNTAX_ERR for invalid reason - https://developer.mozilla.org/en-US/docs/Web/API/WebSocket#Exceptions_thrown
        qCDebug(l) << "Invalid reason";

        return;
    }

    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    if ((code >= 1000 && code <= 1011) || code == 1015) {
        closeCode = static_cast<QWebSocketProtocol::CloseCode>(code);;
    }
    else {
        //TODO throw INVALID_ACCESS_ERR for invalide code - https://developer.mozilla.org/en-US/docs/Web/API/WebSocket#Exceptions_thrown
        qCDebug(l) << "Invalid close code";

        return;
    }

    m_webSocket->close(closeCode, reason);
    m_readyState = CLOSING;
}

void JSKitWebSocket::setOnclose(const QJSValue &onclose)
{
    m_onclose = onclose;
}

void JSKitWebSocket::setOnerror(const QJSValue &onerror)
{
    m_onerror = onerror;
}

void JSKitWebSocket::setOnmessage(const QJSValue &onmessage)
{
    m_onmessage = onmessage;
}

void JSKitWebSocket::setOnopen(const QJSValue &onopen)
{
    m_onopen = onopen;
}

QJSValue JSKitWebSocket::onclose() const
{
    return m_onclose;
}

QJSValue JSKitWebSocket::onerror() const
{
    return m_onerror;
}

QJSValue JSKitWebSocket::onmessage() const
{
    return m_onmessage;
}

QJSValue JSKitWebSocket::onopen() const
{
    return m_onopen;
}

quint32 JSKitWebSocket::bufferedAmount()
{
    return m_bufferedAmount;
}

quint8 JSKitWebSocket::readyState()
{
    return m_readyState;
}

QString JSKitWebSocket::url()
{
    return m_url;
}

void JSKitWebSocket::handleConnected()
{
    m_readyState = OPEN;
    qCDebug(l) << "Connection opened";

    if (m_onopen.isCallable()) {
        qCDebug(l) << "Going to call onopen";

        QJSValueList eventArgs;
        eventArgs.append("open");
        QJSValue event = m_engine->globalObject().property("Event").property("_init").call(eventArgs);

        QJSValueList args;
        args.append(event);
        QJSValue result = m_onopen.callWithInstance(m_engine->newQObject(this), args);
        if (result.isError()) {
            qCWarning(l) << "JS error in onopen handler:" << JSKitManager::describeError(result);
        }
    }
}

void JSKitWebSocket::handleDisconnected()
{
    m_readyState = CLOSED;
    qCDebug(l) << "Connection closed";

    if (m_onclose.isCallable()) {
        qCDebug(l) << "Going to call onclose";

        QJSValueList eventArgs;
        eventArgs.append(QJSValue(true)); //wasClean
        eventArgs.append(QJSValue(m_webSocket->closeCode()));
        eventArgs.append(QJSValue(m_webSocket->closeReason()));

        QJSValue event = m_engine->globalObject().property("CloseEvent").property("_init").call(eventArgs);

        QJSValueList args;
        args.append(event);

        QJSValue result = m_onclose.callWithInstance(m_engine->newQObject(this));
        if (result.isError()) {
            qCWarning(l) << "JS error in onclose handler:" << JSKitManager::describeError(result);
        }
    }
}

void JSKitWebSocket::handleError(QAbstractSocket::SocketError error)
{
    qCDebug(l) << "Error:" << error;

    if (m_onerror.isCallable()) {
        qCDebug(l) << "Going to call onerror";

        QJSValueList eventArgs;
        eventArgs.append(QJSValue("error"));
        QJSValue event = m_engine->globalObject().property("Event").property("_init").call(eventArgs);

        QJSValueList args;
        args.append(event);
        QJSValue result = m_onerror.callWithInstance(m_engine->newQObject(this), args);
        if (result.isError()) {
            qCWarning(l) << "JS error in onclose handler:" << JSKitManager::describeError(result);
        }
    }
}

void JSKitWebSocket::handleSslErrors(const QList<QSslError> &errors)
{
    qCDebug(l) << "Ssl Errors:" << errors;

    if (m_onerror.isCallable()) {
        qCDebug(l) << "Going to call onerror";

        QJSValueList eventArgs;
        eventArgs.append(QJSValue("error"));
        QJSValue event = m_engine->globalObject().property("Event").property("_init").call(eventArgs);

        QJSValueList args;
        args.append(event);
        QJSValue result = m_onerror.callWithInstance(m_engine->newQObject(this), args);
        if (result.isError()) {
            qCWarning(l) << "JS error in onclose handler:" << JSKitManager::describeError(result);
        }
    }
}

void JSKitWebSocket::handleTextMessageReceived(const QString &message)
{
    qCDebug(l) << "Text message recieved: " << message;

    callOnmessage(QJSValue(message));
}

void JSKitWebSocket::handleBinaryMessageReceived(const QByteArray &message)
{
    qCDebug(l) << "Binary message recieved";

    if (m_onmessage.isCallable()) {
        if (m_binaryType == "arraybuffer") {
            QJSValue arrayBufferProto = m_engine->globalObject().property("ArrayBuffer").property("prototype");
            QJSValue arrayBuf = m_engine->newObject();

            if (arrayBufferProto.isUndefined()) {
                qCWarning(l) << "Cannot find proto of ArrayBuffer";
            } else {
                arrayBuf.setPrototype(arrayBufferProto);
                arrayBuf.setProperty("byteLength", m_engine->toScriptValue<uint>(message.size()));

                QJSValue array = m_engine->newArray(message.size());
                for (int i = 0; i < message.size(); i++) {
                    array.setProperty(i, m_engine->toScriptValue<int>(message[i]));
                }

                arrayBuf.setProperty("_bytes", array);
                qCDebug(l) << "calling onmessage with ArrayBuffer of" << message.size() << "bytes";

                callOnmessage(arrayBuf);
            }
        } else {
            qCWarning(l) << "unsupported binaryType:" << m_binaryType;
        }
    }
}

void JSKitWebSocket::callOnmessage(QJSValue data)
{
    if (m_onmessage.isCallable()) {
        qCDebug(l) << "Going to call onmessage";

        QJSValueList eventArgs;
        eventArgs.append(QJSValue(m_webSocket->origin()));
        eventArgs.append(data);

        QJSValue messageEvent = m_engine->globalObject().property("MessageEvent").property("_init").call(eventArgs);

        QJSValueList args;
        args.append(messageEvent);

        QJSValue result = m_onmessage.callWithInstance(m_engine->newQObject(this), args);
        if (result.isError()) {
            qCWarning(l) << "JS error in onmessage handler:" << JSKitManager::describeError(result);
        }
    }
}
