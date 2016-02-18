#ifndef JSKITMANAGER_H
#define JSKITMANAGER_H

#include <QJSEngine>
#include <QPointer>
#include <QLoggingCategory>

#include "../appmanager.h"
#include "../watchconnection.h"
#include "../pebble.h"
#include "../appmsgmanager.h"

#include "jskitconsole.h"
#include "jskitgeolocation.h"
#include "jskitlocalstorage.h"
#include "jskittimer.h"
#include "jskitperformance.h"

class JSKitPebble;

class JSKitManager : public QObject
{
    Q_OBJECT
    QLoggingCategory l;

public:
    explicit JSKitManager(Pebble *pebble, WatchConnection *connection, AppManager *apps, AppMsgManager *appmsg, QObject *parent = 0);
    ~JSKitManager();

    QJSEngine * engine();
    bool isJSKitAppRunning() const;

    static QString describeError(QJSValue error);

    void showConfiguration();
    void handleWebviewClosed(const QString &result);
    void setConfigurationId(const QUuid &uuid);
    AppInfo currentApp();

signals:
    void appNotification(const QUuid &uuid, const QString &title, const QString &body);
    void openURL(const QString &uuid, const QString &url);

private slots:
    void handleAppStarted(const QUuid &uuid);
    void handleAppStopped(const QUuid &uuid);
    void handleAppMessage(const QUuid &uuid, const QVariantMap &msg);

private:
    bool loadJsFile(const QString &filename);
    void startJsApp();
    void stopJsApp();

private:
    friend class JSKitPebble;

    Pebble *m_pebble;
    WatchConnection *m_connection;
    AppManager *m_apps;
    AppMsgManager *m_appmsg;
    AppInfo m_curApp;
    QJSEngine *m_engine;
    QPointer<JSKitPebble> m_jspebble;
    QPointer<JSKitConsole> m_jsconsole;
    QPointer<JSKitLocalStorage> m_jsstorage;
    QPointer<JSKitGeolocation> m_jsgeo;
    QPointer<JSKitTimer> m_jstimer;
    QPointer<JSKitPerformance> m_jsperformance;
    QUuid m_configurationUuid;
};

#endif // JSKITMANAGER_H
