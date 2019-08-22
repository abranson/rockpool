#include <QGuiApplication>
//#include <QQmlApplicationEngine>
#include <QQuickView>
#include <QtQml>
//#include <QFile>

#include <sailfishapp.h>

#include "notificationsourcemodel.h"
#include "servicecontrol.h"
#include "pebbles.h"
#include "pebble.h"
#include "applicationsmodel.h"
#include "applicationsfiltermodel.h"
#include "appstoreclient.h"
#include "screenshotmodel.h"

#include <QTimer>

#ifndef ROCKPOOL_DATA_PATH
#define ROCKPOOL_DATA_PATH "/usr/share/rockpool/"
#endif //ROCKPOOL_DATA_PATH

int main(int argc, char *argv[])
{
    QScopedPointer<const QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setApplicationName("rockpool");
    app->setOrganizationName("");

    QSettings ini(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/"+app->applicationName()+"/app.ini",QSettings::IniFormat);
    QString locale = ini.contains("LANG") ? ini.value("LANG").toString() : QLocale::system().name();
    QTranslator i18n;
    i18n.load("rockpool_"+locale,QString(ROCKPOOL_DATA_PATH)+QString("translations"));
    app->installTranslator(&i18n);

    qmlRegisterUncreatableType<Pebble>("RockPool", 1, 0, "Pebble", "Get them from the model");
    qmlRegisterUncreatableType<ApplicationsModel>("RockPool", 1, 0, "ApplicationsModel", "Get them from a Pebble object");
    qmlRegisterUncreatableType<AppItem>("RockPool", 1, 0, "AppItem", "Get them from an ApplicationsModel");
    qmlRegisterType<ApplicationsFilterModel>("RockPool", 1, 0, "ApplicationsFilterModel");
    qmlRegisterType<Pebbles>("RockPool", 1, 0, "Pebbles");
    qmlRegisterUncreatableType<NotificationSourceModel>("RockPool", 1, 0, "NotificationSourceModel", "Get it from a Pebble object");
    qmlRegisterType<ServiceControl>("RockPool", 1, 0, "ServiceController");
    qmlRegisterType<AppStoreClient>("RockPool", 1, 0, "AppStoreClient");
    qmlRegisterType<ScreenshotModel>("RockPool", 1, 0, "ScreenshotModel");

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty("version", QStringLiteral(VERSION));
    view->rootContext()->setContextProperty("locale", locale);
    view->rootContext()->setContextProperty("appFilePath",QCoreApplication::applicationFilePath());


    view->setSource(SailfishApp::pathTo("qml/rockpool.qml"));
    view->show();

    return app->exec();
}
