#include <QTimerEvent>

#include "jskittimer.h"

JSKitTimer::JSKitTimer(QJSEngine *engine) :
    QObject(engine),
    l(metaObject()->className()),
    m_engine(engine)
{
}

int JSKitTimer::setInterval(QJSValue expression, int delay) //TODO support optional parameters
{
    qCDebug(l) << "Setting interval for " << delay << "ms: " << expression.toString();

    if (expression.isString() || expression.isCallable()) {
        int timerId = startTimer(delay);
        m_intervals.insert(timerId, expression);

        return timerId;
    }

    return -1;
}

void JSKitTimer::clearInterval(int timerId)
{
    qCDebug(l) << "Killing interval " << timerId ;
    killTimer(timerId);
    m_intervals.remove(timerId);
}

int JSKitTimer::setTimeout(QJSValue expression, int delay) //TODO support optional parameters
{
    qCDebug(l) << "Setting timeout for " << delay << "ms: " << expression.toString();

    if (expression.isString() || expression.isCallable()) {
        int timerId = startTimer(delay);
        m_timeouts.insert(timerId, expression);

        return timerId;
    }

    return -1;
}

void JSKitTimer::clearTimeout(int timerId)
{
    qCDebug(l) << "Killing timeout " << timerId ;
    killTimer(timerId);
    m_timeouts.remove(timerId);
}

void JSKitTimer::timerEvent(QTimerEvent *event)
{
    int id = event->timerId();

    QJSValue expression; // find in either intervals or timeouts
    if (m_intervals.contains(id)) {
        expression = m_intervals.value(id);
    } else if (m_timeouts.contains(id)) {
        expression = m_timeouts.value(id);
        killTimer(id); // timeouts don't repeat
    } else {
        qCWarning(l) << "Unknown timer event";
        killTimer(id); // interval nor timeout exist. kill the timer

        return;
    }

    if (expression.isCallable()) { // call it if it's a function
        expression.call().toString();
    }
    else { // otherwise evaluate it
        m_engine->evaluate(expression.toString());
    }
}
