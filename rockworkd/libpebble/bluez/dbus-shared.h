/*
 * Copyright (C) 2013-2015 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 * Charles Kerr <charles.kerr@canonical.com>
 */

#ifndef USS_DBUS_SHARED_H
#define USS_DBUS_SHARED_H

#define DBUS_AGENT_PATH "/com/canonical/SettingsBluetoothAgent"
#define DBUS_ADAPTER_AGENT_PATH "/com/canonical/SettingsBluetoothAgent/adapteragent"
#define DBUS_AGENT_CAPABILITY "KeyboardDisplay"

#define BLUEZ_SERVICE "org.bluez"

#define BLUEZ_ADAPTER_IFACE "org.bluez.Adapter1"
#define BLUEZ_DEVICE_IFACE "org.bluez.Device1"

#define watchCall(call, func) \
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this); \
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, func)

#endif // USS_DBUS_SHARED_H
