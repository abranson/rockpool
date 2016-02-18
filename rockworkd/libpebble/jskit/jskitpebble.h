#ifndef JSKITPEBBLE_P_H
#define JSKITPEBBLE_P_H

#include <QLoggingCategory>

#include "jskitmanager.h"
#include "../appinfo.h"

class JSKitPebble : public QObject
{
    Q_OBJECT
    QLoggingCategory l;

public:
    explicit JSKitPebble(const AppInfo &appInfo, JSKitManager *mgr, QObject *parent=0);

    Q_INVOKABLE void addEventListener(const QString &type, QJSValue function);
    Q_INVOKABLE void removeEventListener(const QString &type, QJSValue function);

    Q_INVOKABLE void showSimpleNotificationOnPebble(const QString &title, const QString &body);
    Q_INVOKABLE uint sendAppMessage(QJSValue message, QJSValue callbackForAck = QJSValue(), QJSValue callbackForNack = QJSValue());

    Q_INVOKABLE void getTimelineToken(QJSValue successCallback = QJSValue(), QJSValue failureCallback = QJSValue());
    Q_INVOKABLE void timelineSubscribe(const QString &topic, QJSValue successCallback = QJSValue(), QJSValue failureCallback = QJSValue());
    Q_INVOKABLE void timelineUnsubscribe(const QString &topic, QJSValue successCallback = QJSValue(), QJSValue failureCallback = QJSValue());
    Q_INVOKABLE void timelineSubscriptions(QJSValue successCallback = QJSValue(), QJSValue failureCallback = QJSValue());

    Q_INVOKABLE QString getAccountToken() const;
    Q_INVOKABLE QString getWatchToken() const;
    Q_INVOKABLE QJSValue getActiveWatchInfo() const;

    Q_INVOKABLE void openURL(const QUrl &url);

    Q_INVOKABLE QJSValue createXMLHttpRequest();
#if QT_VERSION >= 0x050300
    Q_INVOKABLE QJSValue createWebSocket(const QString &url, const QJSValue &protocols=QJSValue{});
#endif
    void invokeCallbacks(const QString &type, const QJSValueList &args = QJSValueList());

private:
    QJSValue buildAckEventObject(uint transaction, const QString &message = QString()) const;

private:
    AppInfo m_appInfo;
    JSKitManager *m_mgr;
    QHash<QString, QList<QJSValue>> m_listeners;
};

#endif // JSKITPEBBLE_P_H
