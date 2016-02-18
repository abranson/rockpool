#ifndef JSKITTIMER_P_H
#define JSKITTIMER_P_H

#include <QLoggingCategory>
#include <QJSValue>
#include <QJSEngine>

class JSKitTimer : public QObject
{
    Q_OBJECT
    QLoggingCategory l;

public:
    explicit JSKitTimer(QJSEngine *engine);

    Q_INVOKABLE int setInterval(QJSValue expression, int delay);
    Q_INVOKABLE void clearInterval(int timerId);

    Q_INVOKABLE int setTimeout(QJSValue expression, int delay);
    Q_INVOKABLE void clearTimeout(int timerId);

protected:
    void timerEvent(QTimerEvent *event);

private:
    QJSEngine *m_engine;
    QHash<int, QJSValue> m_intervals;
    QHash<int, QJSValue> m_timeouts;
};

#endif // JSKITTIMER_P_H
