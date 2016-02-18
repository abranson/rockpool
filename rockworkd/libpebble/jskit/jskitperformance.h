#ifndef JSKITPERFORMANCE_H
#define JSKITPERFORMANCE_H

#include <QObject>
#include <QTime>

class JSKitPerformance : public QObject
{
    Q_OBJECT

public:
    explicit JSKitPerformance(QObject *parent=0);

    Q_INVOKABLE int now();

private:
    QTime m_start;
};

#endif // JSKITPERFORMANCE_H
