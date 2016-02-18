#include <QDebug>

#include "jskitconsole.h"

JSKitConsole::JSKitConsole(QObject *parent) :
    QObject(parent),
    l("JSKit Log"),
    w("JSKit Warning"),
    e("JSKit Error"),
    i("JSKit Info")
{
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8 << msg9;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3 << msg4;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3)
{
    qCDebug(l) << msg0 << msg1 << msg2 << msg3;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1, const QString &msg2)
{
    qCDebug(l) << msg0 << msg1 << msg2;
}

void JSKitConsole::log(const QString &msg0, const QString &msg1)
{
    qCDebug(l) << msg0 << msg1;
}

void JSKitConsole::log(const QString &msg0)
{
    qCDebug(l) << msg0;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8 << msg9;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3 << msg4;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3)
{
    qCWarning(w) << msg0 << msg1 << msg2 << msg3;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1, const QString &msg2)
{
    qCWarning(w) << msg0 << msg1 << msg2;
}

void JSKitConsole::warn(const QString &msg0, const QString &msg1)
{
    qCWarning(w) << msg0 << msg1;
}

void JSKitConsole::warn(const QString &msg0)
{
    qCWarning(w) << msg0;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8 << msg9;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3 << msg4;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3)
{
    qCCritical(e) << msg0 << msg1 << msg2 << msg3;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1, const QString &msg2)
{
    qCCritical(e) << msg0 << msg1 << msg2;
}

void JSKitConsole::error(const QString &msg0, const QString &msg1)
{
    qCCritical(e) << msg0 << msg1;
}

void JSKitConsole::error(const QString &msg0)
{
    qCCritical(e) << msg0;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8, const QString &msg9)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8 << msg9;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7, const QString &msg8)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7 << msg8;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6, const QString &msg7)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6 << msg7;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5, const QString &msg6)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5 << msg6;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4, const QString &msg5)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3 << msg4 << msg5;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3, const QString &msg4)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3 << msg4;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2, const QString &msg3)
{
    qCDebug(i) << msg0 << msg1 << msg2 << msg3;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1, const QString &msg2)
{
    qCDebug(i) << msg0 << msg1 << msg2;
}

void JSKitConsole::info(const QString &msg0, const QString &msg1)
{
    qCDebug(i) << msg0 << msg1;
}

void JSKitConsole::info(const QString &msg0)
{
    qCDebug(i) << msg0;
}
