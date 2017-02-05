/*
 *  libwatchfish - library with common functionality for SailfishOS smartwatch connector programs.
 *  Copyright (C) 2015 Javier S. Pedro <dev.git@javispedro.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NOTIFICATIONMONITOR_H
#define NOTIFICATIONMONITOR_H

#include <QtCore/QLoggingCategory>
#include <QtCore/QMap>
#include <QtCore/QObject>

#include "notifications.h"

namespace watchfish
{

Q_DECLARE_LOGGING_CATEGORY(notificationMonitorCat)

class NotificationMonitorPrivate;

class NotificationMonitor : public QObject
{
	Q_OBJECT
	Q_DECLARE_PRIVATE(NotificationMonitor)

public:
	explicit NotificationMonitor(QObject *parent = 0);
	~NotificationMonitor();

signals:
    /** Emitted when a notification arrives. */
    void notification(watchfish::Notification *n);

private:
	NotificationMonitorPrivate * const d_ptr;
};

}

#endif // NOTIFICATIONMONITOR_H
