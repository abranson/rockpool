/*
 * Copyright 2016 Ruslan N. Marchenko
 *
 * This file is part of rockpool.
 *
 * rockpool is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * rockpool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MODECONTROL_QML_H
#define MODECONTROL_QML_H

#include <QObject>
#include <QDBusInterface>

class ModeControlEntity : public QObject
{
    Q_OBJECT
    //Q_PROPERTY(QString displayState READ displayState NOTIFY displayStateChanged)
    //Q_PROPERTY(QString activeState READ activeState NOTIFY activeStateChanged)
    //Q_PROPERTY(QString callState READ callState NOTIFY callStateChanged)
    //Q_PROPERTY(QString deviceMode READ deviceMode NOTIFY deviceModeChanged)
    //Q_PROPERTY(QString lockMode READ lockMode NOTIFY lockModeChanged)

public:
    ModeControlEntity(QObject *parent = 0);
    ~ModeControlEntity();

    //QString displayState() const; // on, off, dimmed
    QString activeState() const; // active, inactive
    //QString callState() const; // none, ringing, active, service, normal, emergency
    //QString deviceMode() const; // normal, flight, offline, invalid

    bool isActive() const;

Q_SIGNALS:
    //void displayStateChanged();
    void activeStateChanged();
    //void callStateChanged();
    //void deviceModeChanged();
    //void lockModeChanged();

public slots:
    //void blink(const int &duration);
    //void nudge(const int &duration);

private:
    QDBusInterface *m_iface;
    bool m_inactive = true; // pretend inactive to avoid excessive filtering
    QVariant _call_ret(const QString req) const;
private slots:
    //void _displayStateChanged(const QString state);
    void _activeStateChanged(const bool inactive);
    //void _callStateChanged(const QString state);
    //void _deviceModeChanged(const QString mode);
    //void _lockModeChanged(const QString mode);
};

#endif
