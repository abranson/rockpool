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

#ifndef WATCHFISH_WALLTIMEMONITOR_H
#define WATCHFISH_WALLTIMEMONITOR_H

#include <timed-qt5/interface>
#include <timed-qt5/wallclock>

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QDateTime>

namespace watchfish
{

Q_DECLARE_LOGGING_CATEGORY(walltimeMonitorCat)

class WallTimeMonitorPrivate;

class WallTimeMonitor : public QObject
{
	Q_OBJECT
	Q_DECLARE_PRIVATE(WallTimeMonitor)

	Q_PROPERTY(QDateTime time READ time NOTIFY timeChanged)
	Q_PROPERTY(QString timezone READ timezone NOTIFY timezoneChanged)
	Q_PROPERTY(QString timezoneAbbreviation READ timezoneAbbreviation NOTIFY timezoneAbbreviationChanged)
	Q_PROPERTY(int timezoneOffsetFromUtc READ timezoneOffsetFromUtc NOTIFY timezoneOffsetFromUtcChanged)

public:
	explicit WallTimeMonitor(QObject *parent = 0);
	~WallTimeMonitor();

	/** Gets the current wall clock time. */
	QDateTime time() const;

	/** Gets the current timezone name as an IANA ID, e.g. America/Buenos_Aires. */
	QString timezone() const;
	/** Gets the current timezone abbreviation, e.g. CET. */
	QString timezoneAbbreviation() const;
	/** Gets the current offset from UTC including any possible DST. */
	int timezoneOffsetFromUtc() const;

signals:
	void systemTimeChanged();
	void timeChanged();
	void timezoneChanged();
	void timezoneAbbreviationChanged();
	void timezoneOffsetFromUtcChanged();

private:
	WallTimeMonitorPrivate * const d_ptr;
};

class WallTimeMonitorPrivate : public QObject
{
    Q_OBJECT

public:
    WallTimeMonitorPrivate(WallTimeMonitor *q);
    ~WallTimeMonitorPrivate();

    Maemo::Timed::WallClock::Info info;

private slots:
    void handleTimedSettingsChanged(const Maemo::Timed::WallClock::Info & newInfo, bool timeChanged);

private:
    WallTimeMonitor * const q_ptr;
    Q_DECLARE_PUBLIC(WallTimeMonitor)
};

}

#endif // WATCHFISH_WALLTIMEMONITOR_H
