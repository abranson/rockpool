#include "jskitperformance.h"

JSKitPerformance::JSKitPerformance(QObject *parent) :
    QObject(parent),
    m_start(QTime::currentTime())
{
}

int JSKitPerformance::now()
{
    QTime now = QTime::currentTime();
    return m_start.msecsTo(now);
}
