#include <limits>

#include "jskitgeolocation.h"

JSKitGeolocation::JSKitGeolocation(QJSEngine *engine) :
    QObject(engine),
    l(metaObject()->className()),
    m_engine(engine),
    m_source(0),
    m_lastWatcherId(0)
{
}

void JSKitGeolocation::getCurrentPosition(const QJSValue &successCallback, const QJSValue &errorCallback, const QVariantMap &options)
{
    setupWatcher(successCallback, errorCallback, options, true);
}

int JSKitGeolocation::watchPosition(const QJSValue &successCallback, const QJSValue &errorCallback, const QVariantMap &options)
{
    return setupWatcher(successCallback, errorCallback, options, false);
}

void JSKitGeolocation::clearWatch(int watcherId)
{
    removeWatcher(watcherId);
}

void JSKitGeolocation::handleError(QGeoPositionInfoSource::Error error)
{
    qCWarning(l) << "positioning error: " << error;

    if (m_watchers.empty()) {
        qCWarning(l) << "got position error but no one is watching";
        stopAndRemove();
    }
    else {
        QJSValue obj;
        if (error == QGeoPositionInfoSource::AccessError) {
            obj = buildPositionErrorObject(PERMISSION_DENIED, "permission denied");
        } else {
            obj = buildPositionErrorObject(POSITION_UNAVAILABLE, "position unavailable");
        }

        for (auto it = m_watchers.begin(); it != m_watchers.end(); /*no adv*/) {
            invokeCallback(it->errorCallback, obj);

            if (it->once) {
                it = m_watchers.erase(it);
            } else {
                it->timer.restart();
                ++it;
            }
        }
    }
}

void JSKitGeolocation::handlePosition(const QGeoPositionInfo &pos)
{
    qCDebug(l) << "got position at" << pos.timestamp() << "type" << pos.coordinate().type();

    if (m_watchers.empty()) {
        qCWarning(l) << "got position update but no one is watching";
        stopAndRemove();
    }
    else {
        QJSValue obj = buildPositionObject(pos);

        for (auto it = m_watchers.begin(); it != m_watchers.end(); /*no adv*/) {
            invokeCallback(it->successCallback, obj);

            if (it->once) {
                it = m_watchers.erase(it);
            } else {
                it->timer.restart();
                ++it;
            }
        }
    }
}

void JSKitGeolocation::handleTimeout()
{
    qCDebug(l) << "positioning timeout";

    if (m_watchers.empty()) {
        qCWarning(l) << "got position timeout but no one is watching";
        stopAndRemove();
    }
    else {
        QJSValue obj = buildPositionErrorObject(TIMEOUT, "timeout");

        for (auto it = m_watchers.begin(); it != m_watchers.end(); /*no adv*/) {
            if (it->timer.hasExpired(it->timeout)) {
                qCDebug(l) << "positioning timeout for watch" << it->watcherId
                                 << ", watch is" << it->timer.elapsed() << "ms old, timeout is" << it->timeout;
                invokeCallback(it->errorCallback, obj);

                if (it->once) {
                    it = m_watchers.erase(it);
                } else {
                    it->timer.restart();
                    ++it;
                }
            } else {
                ++it;
            }
        }

        QMetaObject::invokeMethod(this, "updateTimeouts", Qt::QueuedConnection);
    }
}

void JSKitGeolocation::updateTimeouts()
{
    int once_timeout = -1, updates_timeout = -1;

    Q_FOREACH(const Watcher &watcher, m_watchers) {
        qint64 rem_timeout = watcher.timeout - watcher.timer.elapsed();
        qCDebug(l) << "watch" << watcher.watcherId << "rem timeout" << rem_timeout;

        if (rem_timeout >= 0) {
            // Make sure the limits aren't too large
            rem_timeout = qMin<qint64>(rem_timeout, std::numeric_limits<int>::max());

            if (watcher.once) {
                once_timeout = once_timeout >= 0 ? qMin<int>(once_timeout, rem_timeout) : rem_timeout;
            } else {
                updates_timeout = updates_timeout >= 0 ? qMin<int>(updates_timeout, rem_timeout) : rem_timeout;
            }
        }
    }

    if (updates_timeout >= 0) {
        qCDebug(l) << "setting location update interval to" << updates_timeout;
        m_source->setUpdateInterval(updates_timeout);
        m_source->startUpdates();
    } else {
        qCDebug(l) << "stopping updates";
        m_source->stopUpdates();
    }

    if (once_timeout >= 0) {
        qCDebug(l) << "requesting single location update with timeout" << once_timeout;
        m_source->requestUpdate(once_timeout);
    }

    if (once_timeout == 0 && updates_timeout == 0) {
        stopAndRemove();
    }
}

int JSKitGeolocation::setupWatcher(const QJSValue &successCallback, const QJSValue &errorCallback, const QVariantMap &options, bool once)
{
    Watcher watcher;
    watcher.successCallback = successCallback;
    watcher.errorCallback = errorCallback;
    watcher.highAccuracy = options.value("enableHighAccuracy", false).toBool();
    watcher.timeout = options.value("timeout", std::numeric_limits<int>::max() - 1).toInt();
    watcher.maximumAge = options.value("maximumAge", 0).toLongLong();
    watcher.once = once;
    watcher.watcherId = ++m_lastWatcherId;

    qCDebug(l) << "setting up watcher, gps=" << watcher.highAccuracy << "timeout=" << watcher.timeout << "maximumAge=" << watcher.maximumAge << "once=" << watcher.once;

    if (!m_source) {
        m_source = QGeoPositionInfoSource::createDefaultSource(this);

        connect(m_source, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(&QGeoPositionInfoSource::error),
                this, &JSKitGeolocation::handleError);
        connect(m_source, &QGeoPositionInfoSource::positionUpdated,
                this, &JSKitGeolocation::handlePosition);
        connect(m_source, &QGeoPositionInfoSource::updateTimeout,
                this, &JSKitGeolocation::handleTimeout);
    }

    if (watcher.maximumAge > 0) {
        QDateTime threshold = QDateTime::currentDateTime().addMSecs(-qint64(watcher.maximumAge));
        QGeoPositionInfo pos = m_source->lastKnownPosition(watcher.highAccuracy);
        qCDebug(l) << "got pos timestamp" << pos.timestamp() << " but we want" << threshold;

        if (pos.isValid() && pos.timestamp() >= threshold) {
            invokeCallback(watcher.successCallback, buildPositionObject(pos));

            if (once) {
                return -1;
            }
        } else if (watcher.timeout == 0 && once) {
            // If the timeout has already expired, and we have no cached data
            // Do not even bother to turn on the GPS; return error object now.
            invokeCallback(watcher.errorCallback, buildPositionErrorObject(TIMEOUT, "no cached position"));
            return -1;
        }
    }

    watcher.timer.start();
    m_watchers.append(watcher);

    qCDebug(l) << "added new watcher" << watcher.watcherId;
    QMetaObject::invokeMethod(this, "updateTimeouts", Qt::QueuedConnection);

    return watcher.watcherId;
}

void JSKitGeolocation::removeWatcher(int watcherId)
{
    Watcher watcher;

    qCDebug(l) << "removing watcherId" << watcher.watcherId;

    for (int i = 0; i < m_watchers.size(); i++) {
        if (m_watchers[i].watcherId == watcherId) {
            watcher = m_watchers.takeAt(i);
            break;
        }
    }

    if (watcher.watcherId != watcherId) {
        qCWarning(l) << "watcherId not found";
        return;
    }

    QMetaObject::invokeMethod(this, "updateTimeouts", Qt::QueuedConnection);
}

QJSValue JSKitGeolocation::buildPositionObject(const QGeoPositionInfo &pos)
{
    QJSValue obj = m_engine->newObject();
    QJSValue coords = m_engine->newObject();
    QJSValue timestamp = m_engine->toScriptValue<quint64>(pos.timestamp().toMSecsSinceEpoch());

    coords.setProperty("latitude", m_engine->toScriptValue(pos.coordinate().latitude()));
    coords.setProperty("longitude", m_engine->toScriptValue(pos.coordinate().longitude()));
    if (pos.coordinate().type() == QGeoCoordinate::Coordinate3D) {
        coords.setProperty("altitude", m_engine->toScriptValue(pos.coordinate().altitude()));
    } else {
        coords.setProperty("altitude", m_engine->toScriptValue<void*>(0));
    }

    coords.setProperty("accuracy", m_engine->toScriptValue(pos.attribute(QGeoPositionInfo::HorizontalAccuracy)));

    if (pos.hasAttribute(QGeoPositionInfo::VerticalAccuracy)) {
        coords.setProperty("altitudeAccuracy", m_engine->toScriptValue(pos.attribute(QGeoPositionInfo::VerticalAccuracy)));
    } else {
        coords.setProperty("altitudeAccuracy", m_engine->toScriptValue<void*>(0));
    }

    if (pos.hasAttribute(QGeoPositionInfo::Direction)) {
        coords.setProperty("heading", m_engine->toScriptValue(pos.attribute(QGeoPositionInfo::Direction)));
    } else {
        coords.setProperty("heading", m_engine->toScriptValue<void*>(0));
    }

    if (pos.hasAttribute(QGeoPositionInfo::GroundSpeed)) {
        coords.setProperty("speed", m_engine->toScriptValue(pos.attribute(QGeoPositionInfo::GroundSpeed)));
    } else {
        coords.setProperty("speed", m_engine->toScriptValue<void*>(0));
    }

    obj.setProperty("coords", coords);
    obj.setProperty("timestamp", timestamp);

    return obj;
}

QJSValue JSKitGeolocation::buildPositionErrorObject(PositionError error, const QString &message)
{
    QJSValue obj = m_engine->newObject();

    obj.setProperty("code", m_engine->toScriptValue<unsigned short>(error));
    obj.setProperty("message", m_engine->toScriptValue(message));

    return obj;
}

void JSKitGeolocation::invokeCallback(QJSValue callback, QJSValue event)
{
    if (callback.isCallable()) {
        qCDebug(l) << "invoking callback" << callback.toString();
        QJSValue result = callback.call(QJSValueList({event}));

        if (result.isError()) {
            qCWarning(l) << "error while invoking callback: " << QString("%1:%2: %3")
                .arg(result.property("fileName").toString())
                .arg(result.property("lineNumber").toInt())
                .arg(result.toString());
        }
    } else {
        qCWarning(l) << "callback is not callable";
    }
}

void JSKitGeolocation::stopAndRemove()
{
    if (m_source) {
        qCDebug(l) << "removing source";

        m_source->stopUpdates();
        m_source->deleteLater();
        m_source = 0;
    }
}
