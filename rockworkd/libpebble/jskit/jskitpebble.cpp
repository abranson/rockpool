#include <QUrl>
#include <QCryptographicHash>
#include <QSettings>

#include "jskitpebble.h"
#include "jskitxmlhttprequest.h"
#if QT_VERSION >= 0x050300
#include "jskitwebsocket.h"
#endif
static const char *token_salt = "0feeb7416d3c4546a19b04bccd8419b1";

JSKitPebble::JSKitPebble(const AppInfo &info, JSKitManager *mgr, QObject *parent) :
    QObject(parent),
    l(metaObject()->className()),
    m_appInfo(info),
    m_mgr(mgr)
{
}

void JSKitPebble::addEventListener(const QString &type, QJSValue function)
{
    m_listeners[type].append(function);
}

void JSKitPebble::removeEventListener(const QString &type, QJSValue function)
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

void JSKitPebble::showSimpleNotificationOnPebble(const QString &title, const QString &body)
{
    qCDebug(l) << "showSimpleNotificationOnPebble" << title << body;
    emit m_mgr->appNotification(m_appInfo.uuid(), title, body);
}

uint JSKitPebble::sendAppMessage(QJSValue message, QJSValue callbackForAck, QJSValue callbackForNack)
{
    QVariantMap data = message.toVariant().toMap();
    QPointer<JSKitPebble> pebbObj = this;
    uint transactionId = m_mgr->m_appmsg->nextTransactionId();

    qCDebug(l) << "sendAppMessage" << data;

    m_mgr->m_appmsg->send(
        m_appInfo.uuid(),
        data,
        [this, pebbObj, transactionId, callbackForAck]() mutable {
            if (pebbObj.isNull()) return;

            if (callbackForAck.isCallable()) {
                QJSValue event = pebbObj->buildAckEventObject(transactionId);
                QJSValue result = callbackForAck.call(QJSValueList({event}));

                if (result.isError()) {
                    qCWarning(l) << "error while invoking ACK callback"
                        << callbackForAck.toString() << ":"
                        << JSKitManager::describeError(result);
                }
            }
        },
        [this, pebbObj, transactionId, callbackForNack]() mutable {
            if (pebbObj.isNull()) return;

            if (callbackForNack.isCallable()) {
                QJSValue event = pebbObj->buildAckEventObject(transactionId, "NACK from watch");
                QJSValue result = callbackForNack.call(QJSValueList({event}));

                if (result.isError()) {
                    qCWarning(l) << "error while invoking NACK callback"
                        << callbackForNack.toString() << ":"
                        << JSKitManager::describeError(result);
                }
            }
        }
    );

    return transactionId;
}

void JSKitPebble::getTimelineToken(QJSValue successCallback, QJSValue failureCallback)
{
    //TODO actually implement this
    qCDebug(l) << "call to unsupported method Pebble.getTimelineToken";
    Q_UNUSED(successCallback);

    if (failureCallback.isCallable()) {
        failureCallback.call();
    }
}

void JSKitPebble::timelineSubscribe(const QString &topic, QJSValue successCallback, QJSValue failureCallback)
{
    //TODO actually implement this
    qCDebug(l) << "call to unsupported method Pebble.timelineSubscribe";
    Q_UNUSED(topic);
    Q_UNUSED(successCallback);

    if (failureCallback.isCallable()) {
        failureCallback.call();
    }
}

void JSKitPebble::timelineUnsubscribe(const QString &topic, QJSValue successCallback, QJSValue failureCallback)
{
    //TODO actually implement this
    qCDebug(l) << "call to unsupported method Pebble.timelineUnsubscribe";
    Q_UNUSED(topic);
    Q_UNUSED(successCallback);

    if (failureCallback.isCallable()) {
        failureCallback.call();
    }
}

void JSKitPebble::timelineSubscriptions(QJSValue successCallback, QJSValue failureCallback)
{
    //TODO actually implement this
    qCDebug(l) << "call to unsupported method Pebble.timelineSubscriptions";
    Q_UNUSED(successCallback);

    if (failureCallback.isCallable()) {
        failureCallback.call();
    }
}


QString JSKitPebble::getAccountToken() const
{
    // We do not have any account system, so we just fake something up.
    QCryptographicHash hasher(QCryptographicHash::Md5);

    hasher.addData(token_salt, strlen(token_salt));
    hasher.addData(m_appInfo.uuid().toByteArray());

    QSettings settings;
    QString token = settings.value("accountToken").toString();

    if (token.isEmpty()) {
        token = QUuid::createUuid().toString();
        qCDebug(l) << "created new account token" << token;
        settings.setValue("accountToken", token);
    }

    hasher.addData(token.toLatin1());

    QString hash = hasher.result().toHex();
    qCDebug(l) << "returning account token" << hash;

    return hash;
}

QString JSKitPebble::getWatchToken() const
{
    QCryptographicHash hasher(QCryptographicHash::Md5);

    hasher.addData(token_salt, strlen(token_salt));
    hasher.addData(m_appInfo.uuid().toByteArray());
    hasher.addData(m_mgr->m_pebble->serialNumber().toLatin1());

    QString hash = hasher.result().toHex();
    qCDebug(l) << "returning watch token" << hash;

    return hash;
}

QJSValue JSKitPebble::getActiveWatchInfo() const
{
    QJSValue watchInfo = m_mgr->m_engine->newObject();

    switch (m_mgr->m_pebble->hardwarePlatform()) {
    case HardwarePlatformBasalt:
        watchInfo.setProperty("platform", "basalt");
        break;

    case HardwarePlatformChalk:
        watchInfo.setProperty("platform", "chalk");
        break;

    default:
        watchInfo.setProperty("platform", "aplite");
        break;
    }

    switch (m_mgr->m_pebble->model()) {
    case ModelTintinWhite:
        watchInfo.setProperty("model", "pebble_white");
        break;

    case ModelTintinRed:
        watchInfo.setProperty("model", "pebble_red");
        break;

    case ModelTintinOrange:
        watchInfo.setProperty("model", "pebble_orange");
        break;

    case ModelTintinGrey:
        watchInfo.setProperty("model", "pebble_grey");
        break;

    case ModelBiancaSilver:
        watchInfo.setProperty("model", "pebble_steel_silver");
        break;

    case ModelBiancaBlack:
        watchInfo.setProperty("model", "pebble_steel_black");
        break;

    case ModelTintinBlue:
        watchInfo.setProperty("model", "pebble_blue");
        break;

    case ModelTintinGreen:
        watchInfo.setProperty("model", "pebble_green");
        break;

    case ModelTintinPink:
        watchInfo.setProperty("model", "pebble_pink");
        break;

    case ModelSnowyWhite:
        watchInfo.setProperty("model", "pebble_time_white");
        break;

    case ModelSnowyBlack:
        watchInfo.setProperty("model", "pebble_time_black");
        break;

    case ModelSnowyRed:
        watchInfo.setProperty("model", "pebble_time_read");
        break;

    case ModelBobbySilver:
        watchInfo.setProperty("model", "pebble_time_steel_silver");
        break;

    case ModelBobbyBlack:
        watchInfo.setProperty("model", "pebble_time_steel_black");
        break;

    case ModelBobbyGold:
        watchInfo.setProperty("model", "pebble_time_steel_gold");
        break;

    case ModelSpalding14Silver:
        watchInfo.setProperty("model", "pebble_time_round_silver_14mm");
        break;

    case ModelSpalding14Black:
        watchInfo.setProperty("model", "pebble_time_round_black_14mm");
        break;

    case ModelSpalding20Silver:
        watchInfo.setProperty("model", "pebble_time_round_silver_20mm");
        break;

    case ModelSpalding20Black:
        watchInfo.setProperty("model", "pebble_time_round_black_20mm");
        break;

    case ModelSpalding14RoseGold:
        watchInfo.setProperty("model", "pebble_time_round_rose_gold_14mm");
        break;

    default:
        watchInfo.setProperty("model", "pebble_black");
        break;
    }

    watchInfo.setProperty("language", m_mgr->m_pebble->language());

    QJSValue firmware = m_mgr->m_engine->newObject();
    QString version = m_mgr->m_pebble->softwareVersion().remove("v");
    QStringList versionParts = version.split(".");

    if (versionParts.count() >= 1) {
        firmware.setProperty("major", versionParts[0].toInt());
    }

    if (versionParts.count() >= 2) {
        firmware.setProperty("minor", versionParts[1].toInt());
    }

    if (versionParts.count() >= 3) {
        if (versionParts[2].contains("-")) {
            QStringList patchParts = version.split("-");
            firmware.setProperty("patch", patchParts[0].toInt());
            firmware.setProperty("suffix", patchParts[1]);
        } else {
            firmware.setProperty("patch", versionParts[2].toInt());
            firmware.setProperty("suffix", "");
        }
    }

    watchInfo.setProperty("firmware", firmware);
    return watchInfo;
}

void JSKitPebble::openURL(const QUrl &url)
{
    emit m_mgr->openURL(m_appInfo.uuid().toString(), url.toString());
}

QJSValue JSKitPebble::createXMLHttpRequest()
{
    JSKitXMLHttpRequest *xhr = new JSKitXMLHttpRequest(m_mgr->engine());
    return m_mgr->engine()->newQObject(xhr);
}

#if QT_VERSION >= 0x050300
QJSValue JSKitPebble::createWebSocket(const QString &url, const QJSValue &protocols)
{
    JSKitWebSocket *ws = new JSKitWebSocket(m_mgr->engine(), url, protocols);
    return m_mgr->engine()->newQObject(ws);
}
#endif


QJSValue JSKitPebble::buildAckEventObject(uint transaction, const QString &message) const
{
    QJSEngine *engine = m_mgr->engine();
    QJSValue eventObj = engine->newObject();
    QJSValue dataObj = engine->newObject();

    dataObj.setProperty("transactionId", engine->toScriptValue(transaction));
    eventObj.setProperty("data", dataObj);

    if (!message.isEmpty()) {
        QJSValue errorObj = engine->newObject();

        errorObj.setProperty("message", engine->toScriptValue(message));
        eventObj.setProperty("error", errorObj);
    }

    return eventObj;
}

void JSKitPebble::invokeCallbacks(const QString &type, const QJSValueList &args)
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
