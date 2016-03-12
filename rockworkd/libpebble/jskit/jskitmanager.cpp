#include <QFile>
#include <QDir>
#include <QUrl>

#include "jskitmanager.h"
#include "jskitpebble.h"

JSKitManager::JSKitManager(Pebble *pebble, WatchConnection *connection, AppManager *apps, AppMsgManager *appmsg, QObject *parent) :
    QObject(parent),
    l(metaObject()->className()),
    m_pebble(pebble),
    m_connection(connection),
    m_apps(apps),
    m_appmsg(appmsg),
    m_engine(0),
    m_configurationUuid(0)
{
    connect(m_appmsg, &AppMsgManager::appStarted, this, &JSKitManager::handleAppStarted);
    connect(m_appmsg, &AppMsgManager::appStopped, this, &JSKitManager::handleAppStopped);
}

JSKitManager::~JSKitManager()
{
    if (m_engine) {
        stopJsApp();
    }
}

QJSEngine * JSKitManager::engine()
{
    return m_engine;
}

bool JSKitManager::isJSKitAppRunning() const
{
    return m_engine != 0;
}

QString JSKitManager::describeError(QJSValue error)
{
    return QString("%1:%2: %3")
        .arg(error.property("fileName").toString())
        .arg(error.property("lineNumber").toInt())
        .arg(error.toString());
}

void JSKitManager::showConfiguration()
{
    if (m_engine) {
        qCDebug(l) << "requesting configuration";
        m_jspebble->invokeCallbacks("showConfiguration");
    } else {
        qCWarning(l) << "requested to show configuration, but JS engine is not running";
    }
}

void JSKitManager::handleWebviewClosed(const QString &result)
{
    if (m_engine) {
        QJSValue eventObj = m_engine->newObject();
        eventObj.setProperty("response", QUrl::fromPercentEncoding(result.toUtf8()));

        qCDebug(l) << "Sending" << eventObj.property("response").toString();
        m_jspebble->invokeCallbacks("webviewclosed", QJSValueList({eventObj}));

        loadJsFile(":/cacheLocalStorage.js");
    } else {
        qCWarning(l) << "webview closed event, but JS engine is not running";
    }
}

void JSKitManager::setConfigurationId(const QUuid &uuid)
{
    m_configurationUuid = uuid;
}

AppInfo JSKitManager::currentApp()
{
    return m_curApp;
}

void JSKitManager::handleAppStarted(const QUuid &uuid)
{
    AppInfo info = m_apps->info(uuid);
    if (!info.uuid().isNull() && info.isJSKit() && info.uuid() != m_curApp.uuid()) {
        qCDebug(l) << "Preparing to start JSKit app" << info.uuid() << info.shortName();

        m_curApp = info;
        startJsApp();
    }
}

void JSKitManager::handleAppStopped(const QUuid &uuid)
{
    if (!m_curApp.uuid().isNull()) {
        if (m_curApp.uuid() != uuid) {
            qCWarning(l) << "Closed app with invalid UUID";
        }

        stopJsApp();
        m_curApp = AppInfo();
        qCDebug(l) << "App stopped" << uuid;
    }
}

void JSKitManager::handleAppMessage(const QUuid &uuid, const QVariantMap &msg)
{
    if (m_curApp.uuid() == uuid) {
        qCDebug(l) << "handling app message" << uuid << msg;

        if (m_engine) {
            QJSValue eventObj = m_engine->newObject();
            QJSValue payload = m_engine->newObject();

            //These variables are up here to avoid cross initialization
            QByteArray byteArray;
            QJSValue array;

            QMapIterator<QString, QVariant> it(msg);
            while (it.hasNext()) {
                it.next();

                switch (int(it.value().type())) {
                case QMetaType::Char:
                case QMetaType::UChar:
                case QMetaType::SChar:
                    payload.setProperty(it.key(), m_engine->toScriptValue(it.value().value<char>()));
                    break;
                case QMetaType::Int:
                case QMetaType::Short:
                case QMetaType::UShort:
                    payload.setProperty(it.key(), m_engine->toScriptValue(it.value().toInt()));
                    break;
                case QMetaType::UInt:
                    payload.setProperty(it.key(), m_engine->toScriptValue(it.value().toUInt()));
                    break;
                case QMetaType::Bool:
                    payload.setProperty(it.key(), m_engine->toScriptValue(it.value().toBool()));
                    break;
                case QMetaType::Float:
                case QMetaType::Double:
                    payload.setProperty(it.key(), m_engine->toScriptValue(it.value().toDouble()));
                    break;
                case QMetaType::QByteArray:
                    byteArray = it.value().toByteArray();

                    array = m_engine->newArray(byteArray.size());
                    for (int i = 0; i < byteArray.size(); i++) {
                        array.setProperty(i, m_engine->toScriptValue<int>(byteArray[i]));
                    }

                    payload.setProperty(it.key(), array);

                    break;
                case QMetaType::QString:
                    payload.setProperty(it.key(), m_engine->toScriptValue(it.value().toString()));
                    break;
                default:
                    qWarning() << "JSKitManager::handleAppMessage" << "Unknown dict item type:" << it.value().typeName();
                    break;
                }
            }

            //eventObj.setProperty("payload", m_engine->toScriptValue(msg));
            eventObj.setProperty("payload", payload);

            m_jspebble->invokeCallbacks("appmessage", QJSValueList({eventObj}));

            loadJsFile(":/cacheLocalStorage.js");
        }
        else {
            qCDebug(l) << "but engine is stopped";
        }
    }
}

bool JSKitManager::loadJsFile(const QString &filename)
{
    Q_ASSERT(m_engine);

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(l) << "Failed to load JS file:" << file.fileName();
        return false;
    }

    qCDebug(l) << "evaluating js file" << file.fileName();

    QJSValue result = m_engine->evaluate(QString::fromUtf8(file.readAll()), file.fileName());
    if (result.isError()) {
        qCWarning(l) << "error while evaluating JS script:" << describeError(result);
        return false;
    }

    qCDebug(l) << "JS script evaluated";
    return true;
}

void JSKitManager::startJsApp()
{
    if (m_engine) stopJsApp();

    if (m_curApp.uuid().isNull()) {
        qCWarning(l) << "Attempting to start JS app with invalid UUID";
        return;
    }

    m_engine = new QJSEngine(this);
    m_jspebble = new JSKitPebble(m_curApp, this, m_engine);
    m_jsconsole = new JSKitConsole(m_engine);
    m_jsstorage = new JSKitLocalStorage(m_engine, m_pebble->storagePath(), m_curApp.uuid());
    m_jsgeo = new JSKitGeolocation(m_engine);
    m_jstimer = new JSKitTimer(m_engine);
    m_jsperformance = new JSKitPerformance(m_engine);

    qCDebug(l) << "starting JS app" << m_curApp.shortName();

    QJSValue globalObj = m_engine->globalObject();
    QJSValue jskitObj = m_engine->newObject();

    jskitObj.setProperty("pebble", m_engine->newQObject(m_jspebble));
    jskitObj.setProperty("console", m_engine->newQObject(m_jsconsole));
    jskitObj.setProperty("localstorage", m_engine->newQObject(m_jsstorage));
    jskitObj.setProperty("geolocation", m_engine->newQObject(m_jsgeo));
    jskitObj.setProperty("timer", m_engine->newQObject(m_jstimer));
    jskitObj.setProperty("performance", m_engine->newQObject(m_jsperformance));
    globalObj.setProperty("_jskit", jskitObj);

    QJSValue navigatorObj = m_engine->newObject();
    navigatorObj.setProperty("language", m_engine->toScriptValue(QLocale().name()));
    globalObj.setProperty("navigator", navigatorObj);

    // Set this.window = this
    globalObj.setProperty("window", globalObj);

    // Shims for compatibility...
    loadJsFile(":/jskitsetup.js");

    // Polyfills...
    loadJsFile(":/typedarray.js");

    // Now the actual script
    QString jsApp = m_curApp.file(AppInfo::FileTypeJsApp, HardwarePlatformUnknown);
    QFile f(jsApp);
    if (!f.open(QFile::ReadOnly)) {
        qCWarning(l) << "Error opening" << jsApp;
        return;
    }
    QJSValue ret = m_engine->evaluate(QString::fromUtf8(f.readAll()));
    qCDebug(l) << "loaded script" << ret.toString();

    // Setup the message callback
    QUuid uuid = m_curApp.uuid();
    m_appmsg->setMessageHandler(uuid, [this, uuid](const QVariantMap &msg) {
        QMetaObject::invokeMethod(this, "handleAppMessage", Qt::QueuedConnection,
                                  Q_ARG(QUuid, uuid),
                                  Q_ARG(QVariantMap, msg));

        // Invoke the slot as a queued connection to give time for the ACK message
        // to go through first.

        return true;
    });

    // We try to invoke the callbacks even if script parsing resulted in error...
    m_jspebble->invokeCallbacks("ready");

    loadJsFile(":/cacheLocalStorage.js");

    if (m_configurationUuid == m_curApp.uuid()) {
        qCDebug(l) << "going to launch config for" << m_configurationUuid;
        showConfiguration();
    }

    m_configurationUuid = QUuid();
}

void JSKitManager::stopJsApp()
{
    qCDebug(l) << "stop js app" << m_curApp.uuid();
    if (!m_engine) return; // Nothing to do!

    loadJsFile(":/cacheLocalStorage.js");

    if (!m_curApp.uuid().isNull()) {
        m_appmsg->clearMessageHandler(m_curApp.uuid());
    }

    m_engine->collectGarbage();
    m_engine->deleteLater();
    m_engine = 0;
}
