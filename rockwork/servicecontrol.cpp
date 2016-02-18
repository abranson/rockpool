#include "servicecontrol.h"

#include <QFile>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QProcess>
#include <QDBusReply>

ServiceControl::ServiceControl(QObject *parent) : QObject(parent),
    systemd(new QDBusInterface("org.freedesktop.systemd1",
                               "/org/freedesktop/systemd1",
                               "org.freedesktop.systemd1.Manager",
                               QDBusConnection::sessionBus(), this))
{
    systemd->call("Subscribe");

    QDBusReply<QDBusObjectPath> unit = systemd->call("LoadUnit", ROCKPOOLD_SYSTEMD_UNIT);
    if (unit.isValid()) {
        unitPath = unit.value();

        getUnitProperties();

        QDBusConnection::sessionBus().connect(
                    "org.freedesktop.systemd1", unitPath.path(),
                    "org.freedesktop.DBus.Properties", "PropertiesChanged",
                    this, SLOT(onPropertiesChanged(QString,QMap<QString,QVariant>,QStringList)));
    } else {
        qWarning() << unit.error().message();
    }
}

void ServiceControl::onPropertiesChanged(QString interface, QMap<QString,QVariant> changed, QStringList invalidated)
{
    qDebug() << Q_FUNC_INFO << interface << changed << invalidated;
    if (interface != "org.freedesktop.systemd1.Unit") return;
    if (invalidated.contains("UnitFileState") || invalidated.contains("ActiveState"))
        getUnitProperties();
}

bool ServiceControl::serviceRunning() const
{
    return unitProperties["UnitFileState"].toString() == "enabled";
}

bool ServiceControl::setServiceRunning(bool running)
{
    if (running && !serviceRunning()) {
        return startService();
    } else if (!running && serviceRunning()) {
        return stopService();
    }
    return true; // Requested state is already the current state.
}

bool ServiceControl::startService()
{
    QDBusError reply;
    systemd->call("EnableUnitFiles", QStringList() << ROCKPOOLD_SYSTEMD_UNIT, false, true);
    if (reply.isValid()) {
        qWarning() << reply.message();
        return false;
    } else {
        systemd->call("Reload");
        return true;
    }
}

bool ServiceControl::stopService()
{
    QDBusError reply;
    systemd->call("DisableUnitFiles", QStringList() << ROCKPOOLD_SYSTEMD_UNIT, false);
    if (reply.isValid()) {
        qWarning() << reply.message();
        return false;
    } else {
        systemd->call("Reload");
        return true;
    }
}

bool ServiceControl::restartService()
{
    return stopService() && startService();
}

void ServiceControl::getUnitProperties()
{
    QDBusMessage request = QDBusMessage::createMethodCall(
                "org.freedesktop.systemd1", unitPath.path(),
                "org.freedesktop.DBus.Properties", "GetAll");
    request << "org.freedesktop.systemd1.Unit";
    QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(request);
    if (reply.isValid()) {
        QVariantMap newProperties = reply.value();
      //bool emitEnabledChanged = (unitProperties["UnitFileState"] != newProperties["UnitFileState"]);
        bool emitRunningChanged = (unitProperties["ActiveState"] != newProperties["ActiveState"]);
        unitProperties = newProperties;
        if (emitRunningChanged) emit serviceRunning();
    } else {
        qWarning() << reply.error().message();
    }
}
