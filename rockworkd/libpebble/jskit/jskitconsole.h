#ifndef JSKITCONSOLE_H
#define JSKITCONSOLE_H

#include <QLoggingCategory>

/*
  We opted to do multiple overloaded functions rather than one with default
  arguments as this method produces nicer log messages and wont omit (possibly)
  important messages like empty string, undefined, or null.
*/

class JSKitConsole : public QObject
{
    Q_OBJECT
    QLoggingCategory l;
    QLoggingCategory w;
    QLoggingCategory e;
    QLoggingCategory i;

public:
    explicit JSKitConsole(QObject *parent=0);

    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1, const QString &msg2);
    Q_INVOKABLE void log(const QString &msg0, const QString &msg1);
    Q_INVOKABLE void log(const QString &msg0);

    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1, const QString &msg2);
    Q_INVOKABLE void warn(const QString &msg0, const QString &msg1);
    Q_INVOKABLE void warn(const QString &msg0);

    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1, const QString &msg2);
    Q_INVOKABLE void error(const QString &msg0, const QString &msg1);
    Q_INVOKABLE void error(const QString &msg0);

    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1, const QString &msg2);
    Q_INVOKABLE void info(const QString &msg0, const QString &msg1);
    Q_INVOKABLE void info(const QString &msg0);
};

#endif // JSKITCONSOLE_H
