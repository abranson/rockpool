#include <QFileInfo>
#include <QtQuick>

#include <sailfishapp.h>

#include "notificationsourcemodel.h"
#include "servicecontrol.h"
#include "pebbles.h"
#include "pebble.h"
#include "applicationsmodel.h"
#include "applicationsfiltermodel.h"
#include "appstoreclient.h"
#include "corestoreaccount.h"
#include "screenshotmodel.h"
#include "uiactivation.h"
#include <QDBusConnection>
#include <QTimer>

#ifndef ROCKPOOL_DATA_PATH
#define ROCKPOOL_DATA_PATH "/usr/share/rockpool/"
#endif //ROCKPOOL_DATA_PATH

int main(int argc, char *argv[])
{
    QScopedPointer<const QGuiApplication> app(SailfishApp::application(argc, argv));
    QGuiApplication::setApplicationName("Rockpool");
    QGuiApplication::setOrganizationName("uk.co.nationalfantastic");

    QSettings ini(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                  + QStringLiteral("/app.ini"), QSettings::IniFormat);
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
    qmlRegisterType<CoreStoreAccount>("RockPool", 1, 0, "CoreStoreAccount");
    qmlRegisterType<AppStoreClient>("RockPool", 1, 0, "AppStoreClient");
    qmlRegisterType<ScreenshotModel>("RockPool", 1, 0, "ScreenshotModel");

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    UiActivation activation;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/rockpool/UI"), &activation,
                            QDBusConnection::ExportAllSlots)
            || !bus.registerService(QStringLiteral("org.rockpool.UI"))) {
        qWarning() << "Could not register Rockpool UI activation";
    }
    QObject::connect(&activation, &UiActivation::firmwareRequested, view.data(), [&view]() {
        view->show();
        view->raise();
        view->requestActivate();
    });
    QObject::connect(&activation, &UiActivation::appUpdatesRequested, view.data(), [&view]() {
        view->show();
        view->raise();
        view->requestActivate();
    });
    QObject::connect(&activation, &UiActivation::installReviewRequested, view.data(), [&view]() {
        view->show();
        view->raise();
        view->requestActivate();
    });
    // Sailjail hides installer executables. The installed runtime unit remains visible
    // even when the user has stopped AppSupport.
    const bool appSupportInstalled =
            QFileInfo::exists(QStringLiteral("/usr/lib/systemd/system/appsupport@.service"))
            || QFileInfo::exists(QStringLiteral("/usr/lib/systemd/system/aliendalvik.service"))
            || QFileInfo::exists(QStringLiteral("/usr/bin/apkd-install"));
    view->rootContext()->setContextProperty("appSupportInstalled", appSupportInstalled);
    view->rootContext()->setContextProperty("uiActivation", &activation);
    view->rootContext()->setContextProperty("version", QStringLiteral(VERSION));
    view->rootContext()->setContextProperty("locale", locale);
    view->rootContext()->setContextProperty("appFilePath",QCoreApplication::applicationFilePath());

    view->setSource(SailfishApp::pathTo("qml/rockpool.qml"));
    view->show();

    return app->exec();
}
