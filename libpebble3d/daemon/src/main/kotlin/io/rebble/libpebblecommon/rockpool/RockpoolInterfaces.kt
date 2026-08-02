/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import org.freedesktop.dbus.DBusPath
import org.freedesktop.dbus.FileDescriptor
import org.freedesktop.dbus.TypeRef
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.annotations.DBusProperties
import org.freedesktop.dbus.annotations.DBusProperty
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant

/** Type tokens keep dbus-java's generated introspection signatures precise. */
internal class RockpoolObjectPathList : TypeRef<List<DBusPath>>
internal class RockpoolStringList : TypeRef<List<String>>
internal class RockpoolRecordList : TypeRef<List<Map<String, Variant<*>>>>
internal class RockpoolVariantMap : TypeRef<Map<String, Variant<*>>>

@DBusInterfaceName("org.rockpool.Manager1")
@DBusProperties(
    DBusProperty(name = "ApiVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "DaemonVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "State", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Watches", type = RockpoolObjectPathList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Capabilities", type = RockpoolStringList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LastError", type = String::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolManager1 : DBusInterface {
    fun Refresh(): DBusPath
}

@DBusInterfaceName("org.rockpool.Discovery1")
@DBusProperties(
    DBusProperty(name = "Scanning", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ScanResults", type = RockpoolRecordList::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolDiscovery1 : DBusInterface {
    fun StartScan(options: Map<String, Variant<*>>): DBusPath
    fun StopScan(): DBusPath
    fun Pair(candidate: Map<String, Variant<*>>): DBusPath
    fun CancelPairing(): DBusPath
    fun ImportBondedWatches(): DBusPath

    class PairingPrompt(
        path: String,
        operation: DBusPath,
        prompt: Map<String, Variant<*>>,
    ) : DBusSignal(path, operation, prompt)
}

@DBusInterfaceName("org.rockpool.Account1")
@DBusProperties(
    DBusProperty(name = "Authenticated", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Name", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Email", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "SyncState", type = String::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolAccount1 : DBusInterface {
    /** The OAuth value is deliberately write-only. */
    fun SetOAuthToken(token: String): DBusPath
    fun Sync(): DBusPath
}

@DBusInterfaceName("org.rockpool.Platform1")
@DBusProperties(
    DBusProperty(name = "Provider", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "AbiVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "BuildId", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "HelperPid", type = UInt32::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Health", type = RockpoolVariantMap::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Capabilities", type = RockpoolStringList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LastError", type = String::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolPlatform1 : DBusInterface {
    fun Restart(): DBusPath
}

@DBusInterfaceName("org.rockpool.Watch1")
@DBusProperties(
    DBusProperty(name = "Id", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Name", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Serial", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Transport", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ConnectionState", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LastError", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Capabilities", type = RockpoolStringList::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolWatch1 : DBusInterface {
    fun Connect(): DBusPath
    fun Disconnect(): DBusPath
    fun Forget(): DBusPath
}

@DBusInterfaceName("org.rockpool.Firmware1")
@DBusProperties(
    DBusProperty(name = "FirmwareVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LanguageVersion", type = String::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolFirmware1 : DBusInterface {
    fun InstallFirmware(firmware: FileDescriptor): DBusPath
    fun InstallLanguagePack(languagePack: FileDescriptor): DBusPath
}

@DBusInterfaceName("org.rockpool.Applications1")
@DBusProperties(
    DBusProperty(name = "Applications", type = RockpoolRecordList::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolApplications1 : DBusInterface {
    fun Install(pbw: FileDescriptor): DBusPath
    fun Remove(uuid: String): DBusPath
}

@DBusInterfaceName("org.rockpool.Timeline1")
@DBusProperties(
    DBusProperty(name = "CalendarEnabled", type = Boolean::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolTimeline1 : DBusInterface {
    fun Sync(): DBusPath
}

@DBusInterfaceName("org.rockpool.Notifications1")
@DBusProperties(
    DBusProperty(name = "Filters", type = RockpoolRecordList::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolNotifications1 : DBusInterface {
    fun SetFilters(filters: List<Map<String, Variant<*>>>): DBusPath
}

@DBusInterfaceName("org.rockpool.Messaging1")
@DBusProperties(
    DBusProperty(name = "CannedResponses", type = RockpoolRecordList::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolMessaging1 : DBusInterface {
    fun SetCannedResponses(responses: List<Map<String, Variant<*>>>): DBusPath
}

@DBusInterfaceName("org.rockpool.Health1")
@DBusProperties(
    DBusProperty(name = "Settings", type = RockpoolVariantMap::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolHealth1 : DBusInterface {
    fun SetSettings(settings: Map<String, Variant<*>>): DBusPath
}

@DBusInterfaceName("org.rockpool.Profiles1")
@DBusProperties(
    DBusProperty(name = "Profiles", type = RockpoolVariantMap::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolProfiles1 : DBusInterface {
    fun SetProfiles(profiles: Map<String, Variant<*>>): DBusPath
}

@DBusInterfaceName("org.rockpool.Screenshots1")
@DBusProperties(
    DBusProperty(name = "Screenshots", type = RockpoolRecordList::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolScreenshots1 : DBusInterface {
    fun Capture(): DBusPath
}

@DBusInterfaceName("org.rockpool.Logs1")
@DBusProperties(
    DBusProperty(
        name = "DumpPath",
        type = String::class,
        access = DBusProperty.Access.READ,
    ),
)
internal interface RockpoolLogs1 : DBusInterface {
    /** Dumps only to ~/Downloads/pebble.log; it accepts no caller-controlled path. */
    fun Dump(): DBusPath
}

@DBusInterfaceName("org.rockpool.Developer1")
@DBusProperties(
    DBusProperty(name = "LocalEnabled", type = Boolean::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolDeveloper1 : DBusInterface {
    fun SetLocalEnabled(enabled: Boolean): DBusPath
}

@DBusInterfaceName("org.rockpool.Operation1")
@DBusProperties(
    DBusProperty(name = "Kind", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "State", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Progress", type = Double::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Result", type = RockpoolVariantMap::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Error", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ErrorDetail", type = String::class, access = DBusProperty.Access.READ),
)
internal interface RockpoolOperation1 : DBusInterface {
    fun Cancel()

    class Completed(path: String, success: Boolean) : DBusSignal(path, success)
}
