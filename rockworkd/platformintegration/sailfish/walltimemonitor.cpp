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

/*
 * Parts of this file based on nemo-qml-plugin-time
 * Copyright (C) 2012 Jolla Ltd.
 * Contact: Martin Jones <martin.jones@jollamobile.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Jolla Ltd. nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include <QtCore/QMessageLogger>

#include "walltimemonitor.h"

namespace watchfish
{

Q_LOGGING_CATEGORY(walltimeMonitorCat, "watchfish-WallTimeMonitor")

WallTimeMonitorPrivate::WallTimeMonitorPrivate(WallTimeMonitor *q)
	: q_ptr(q)
{
	Maemo::Timed::Interface ifc;
	ifc.settings_changed_connect(this, SLOT(handleTimedSettingsChanged(Maemo::Timed::WallClock::Info,bool)));

	QDBusReply<Maemo::Timed::WallClock::Info> reply = ifc.get_wall_clock_info_sync();
	if (reply.isValid()) {
		info = reply.value();
	} else {
		qCWarning(walltimeMonitorCat) << "D-Bus error while contacting timed:" << reply.error().message();
	}
}

WallTimeMonitorPrivate::~WallTimeMonitorPrivate()
{
	Maemo::Timed::Interface ifc;
	ifc.settings_changed_disconnect(this, SLOT(handleTimedSettingsChanged(Maemo::Timed::WallClock::Info,bool)));
}

void WallTimeMonitorPrivate::handleTimedSettingsChanged(const Maemo::Timed::WallClock::Info &newInfo, bool timeChanged)
{
	Q_Q(WallTimeMonitor);

	bool tzChange = newInfo.humanReadableTz() != info.humanReadableTz();
	bool tzaChange = newInfo.tzAbbreviation() != info.tzAbbreviation();
	bool tzoChange = newInfo.secondsEastOfGmt() != info.secondsEastOfGmt();
	bool hourModeChange = newInfo.flagFormat24() != info.flagFormat24();

	info = newInfo;

	if (tzChange)
		emit q->timezoneChanged();
	if (tzaChange)
		emit q->timezoneAbbreviationChanged();
	if (tzoChange)
		emit q->timezoneOffsetFromUtcChanged();
	if (timeChanged)
		emit q->systemTimeChanged();
	if (tzChange || tzaChange || tzoChange || timeChanged || hourModeChange)
		emit q->timeChanged();
}

WallTimeMonitor::WallTimeMonitor(QObject *parent)
	: QObject(parent), d_ptr(new WallTimeMonitorPrivate(this))
{

}

WallTimeMonitor::~WallTimeMonitor()
{
	delete d_ptr;
}

QDateTime WallTimeMonitor::time() const
{
	return QDateTime::currentDateTime();
}

QString WallTimeMonitor::timezone() const
{
	Q_D(const WallTimeMonitor);
	return d->info.humanReadableTz();
}

QString WallTimeMonitor::timezoneAbbreviation() const
{
	Q_D(const WallTimeMonitor);
	return d->info.tzAbbreviation();
}

int WallTimeMonitor::timezoneOffsetFromUtc() const
{
	Q_D(const WallTimeMonitor);
	return d->info.secondsEastOfGmt();
}

}
