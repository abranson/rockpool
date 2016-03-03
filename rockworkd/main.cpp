#include <QCoreApplication>
#include "core.h"

#ifdef ENABLE_TESTING
#include <QGuiApplication>
#endif

Q_DECL_EXPORT int main(int argc, char *argv[])
{

#ifdef ENABLE_TESTING
    QGuiApplication a(argc, argv);
#else
    QCoreApplication a(argc, argv);
#endif

    Core::instance()->init();

    return a.exec();
}

