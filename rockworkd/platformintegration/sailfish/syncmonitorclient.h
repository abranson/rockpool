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

#ifndef SYNCMONITOR_QML_H
#define SYNCMONITOR_QML_H

#include <QObject>
#include <QDBusInterface>

class SyncMonitorClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QStringList enabledServices READ enabledServices NOTIFY enabledServicesChanged)

public:
    SyncMonitorClient(QObject *parent = 0);
    ~SyncMonitorClient();

    QString state() const;
    QStringList enabledServices() const;

Q_SIGNALS:
    void stateChanged();
    void enabledServicesChanged();

public Q_SLOTS:
    void sync(const QStringList &services);
    void cancel(const QStringList &services);
    bool serviceIsEnabled(const QString &service);

private:
    QDBusInterface *m_iface;
};

#endif
