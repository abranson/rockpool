#ifndef JSKITGEOLOCATION_H
#define JSKITGEOLOCATION_H

#include <QElapsedTimer>
#include <QGeoPositionInfoSource>
#include <QJSValue>
#include <QLoggingCategory>
#include <QJSEngine>

class JSKitGeolocation : public QObject
{
    Q_OBJECT
    QLoggingCategory l;

    struct Watcher;

public:
    explicit JSKitGeolocation(QJSEngine *engine);

    enum PositionError {
        PERMISSION_DENIED = 1,
        POSITION_UNAVAILABLE = 2,
        TIMEOUT = 3
    };
    Q_ENUMS(PositionError);

    Q_INVOKABLE void getCurrentPosition(const QJSValue &successCallback, const QJSValue &errorCallback = QJSValue(), const QVariantMap &options = QVariantMap());
    Q_INVOKABLE int watchPosition(const QJSValue &successCallback, const QJSValue &errorCallback = QJSValue(), const QVariantMap &options = QVariantMap());
    Q_INVOKABLE void clearWatch(int watcherId);

private slots:
    void handleError(const QGeoPositionInfoSource::Error error);
    void handlePosition(const QGeoPositionInfo &pos);
    void handleTimeout();
    void updateTimeouts();

private:
    int setupWatcher(const QJSValue &successCallback, const QJSValue &errorCallback, const QVariantMap &options, bool once);
    void removeWatcher(int watcherId);

    QJSValue buildPositionObject(const QGeoPositionInfo &pos);
    QJSValue buildPositionErrorObject(PositionError error, const QString &message = QString());
    QJSValue buildPositionErrorObject(const QGeoPositionInfoSource::Error error);
    void invokeCallback(QJSValue callback, QJSValue event);
    void stopAndRemove();

private:
    QJSEngine *m_engine;
    QGeoPositionInfoSource *m_source;

    struct Watcher {
        QJSValue successCallback;
        QJSValue errorCallback;
        int watcherId;
        bool once;
        bool highAccuracy;
        int timeout;
        QElapsedTimer timer;
        qlonglong maximumAge;
    };

    QList<Watcher> m_watchers;
    int m_lastWatcherId;
};

#endif // JSKITGEOLOCATION_H
