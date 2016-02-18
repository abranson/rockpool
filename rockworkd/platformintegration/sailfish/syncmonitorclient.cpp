/*
 * Copyright 2014 Canonical Ltd.
 *
 * This file is part of sync-monitor.
 *
 * sync-monitor is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * contact-service-app is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDebug>
#include <QTimer>

#include "syncmonitorclient.h"

#define SYNCMONITOR_DBUS_SERVICE_NAME   "com.canonical.SyncMonitor"
#define SYNCMONITOR_DBUS_OBJECT_PATH    "/com/canonical/SyncMonitor"
#define SYNCMONITOR_DBUS_INTERFACE      "com.canonical.SyncMonitor"


SyncMonitorClient::SyncMonitorClient(QObject *parent)
    : QObject(parent),
      m_iface(0)
{
    m_iface = new QDBusInterface(SYNCMONITOR_DBUS_SERVICE_NAME,
                                 SYNCMONITOR_DBUS_OBJECT_PATH,
                                 SYNCMONITOR_DBUS_INTERFACE);
    if (m_iface->lastError().isValid()) {
        qWarning() << "Fail to connect with sync monitor:" << m_iface->lastError();
        return;
    }

    connect(m_iface, SIGNAL(stateChanged()), SIGNAL(stateChanged()));
    connect(m_iface, SIGNAL(enabledServicesChanged()), SIGNAL(enabledServicesChanged()));
    m_iface->call("attach");
}

SyncMonitorClient::~SyncMonitorClient()
{
    if (m_iface) {
        m_iface->call("detach");
        delete m_iface;
        m_iface = 0;
    }
}

QString SyncMonitorClient::state() const
{
    if (m_iface) {
        return m_iface->property("state").toString();
    } else {
        return "";
    }
}

QStringList SyncMonitorClient::enabledServices() const
{
    if (m_iface) {
        return m_iface->property("enabledServices").toStringList();
    } else {
        return QStringList();
    }
}

/*!
  Start a new sync for specified services
*/
void SyncMonitorClient::sync(const QStringList &services)
{
    if (m_iface) {
        qDebug() << "starting sync!";
        m_iface->call("sync", services);
    }
}

/*!
  Cancel current sync for specified services
*/
void SyncMonitorClient::cancel(const QStringList &services)
{
    if (m_iface) {
        m_iface->call("cancel", services);
    }
}

/*!
  Chek if a specific service is enabled or not
*/
bool SyncMonitorClient::serviceIsEnabled(const QString &service)
{
    return enabledServices().contains(service);
}
