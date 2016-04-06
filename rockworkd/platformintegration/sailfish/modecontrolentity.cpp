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

#include <QDebug>
#include "modecontrolentity.h"
#include "nokia-mce-dbus-names.h"

ModeControlEntity::ModeControlEntity(QObject *parent)
    : QObject(parent),
      m_iface(0)
{
    m_iface = new QDBusInterface(MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF, QDBusConnection::systemBus(), this);
    if (m_iface->lastError().isValid()) {
        qWarning() << "Fail to connect with mce.request:" << m_iface->lastError();
        return;
    }

    //QDBusConnection::systemBus().connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_DISPLAY_SIG, this, SLOT(_displayStateChanged));
    QDBusConnection::systemBus().connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_INACTIVITY_SIG, this, SLOT(_activeStateChanged));
    //QDBusConnection::systemBus().connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_CALL_STATE_SIG, this, SLOT(_callStateChanged));
    //QDBusConnection::systemBus().connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_TKLOCK_MODE_SIG, this, SLOT(_lockModeChanged));
    //QDBusConnection::systemBus().connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_DEVICE_MODE_SIG, this, SLOT(_deviceModeChanged));
    m_inactive = _call_ret(MCE_INACTIVITY_STATUS_GET).toBool();
}

ModeControlEntity::~ModeControlEntity()
{
    if (m_iface) {
        delete m_iface;
        m_iface = 0;
    }
}
QVariant ModeControlEntity::_call_ret(const QString req) const
{
    if (!m_iface) {
        return QVariant();
    }
    QDBusMessage m = m_iface->call(req);
    if (m.type() == QDBusMessage::ErrorMessage || m.arguments().count() == 0) {
        qWarning() << "Could not get activity state:" << m.errorMessage();
        return QVariant();
    }
    return m.arguments().first();
}

void ModeControlEntity::_activeStateChanged(const bool inactive)
{
    if(m_inactive != inactive) {
        m_inactive = inactive;
        emit activeStateChanged();
    }
}

bool ModeControlEntity::isActive() const
{
    return !m_inactive;
}
