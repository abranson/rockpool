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
internal class LibPebble3ObjectPathList : TypeRef<List<DBusPath>>
internal class LibPebble3StringList : TypeRef<List<String>>
internal class LibPebble3RecordList : TypeRef<List<Map<String, Variant<*>>>>
internal class LibPebble3VariantMap : TypeRef<Map<String, Variant<*>>>

@DBusInterfaceName("io.rebble.libpebble3.Manager1")
@DBusProperties(
    DBusProperty(name = "ApiVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "DaemonVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "State", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Watches", type = LibPebble3ObjectPathList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Capabilities", type = LibPebble3StringList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LastError", type = String::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Manager1 : DBusInterface {
    fun Refresh(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Discovery1")
@DBusProperties(
    DBusProperty(name = "Scanning", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ScanResults", type = LibPebble3RecordList::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Discovery1 : DBusInterface {
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

@DBusInterfaceName("io.rebble.libpebble3.Account1")
@DBusProperties(
    DBusProperty(name = "Authenticated", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Name", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Email", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "SyncState", type = String::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Account1 : DBusInterface {
    /** The OAuth value is deliberately write-only. */
    fun SetOAuthToken(token: String): DBusPath
    fun Sync(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Platform1")
@DBusProperties(
    DBusProperty(name = "Provider", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "AbiVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "BuildId", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "HelperPid", type = UInt32::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Health", type = LibPebble3VariantMap::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Capabilities", type = LibPebble3StringList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LastError", type = String::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Platform1 : DBusInterface {
    fun Restart(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Watch1")
@DBusProperties(
    DBusProperty(name = "Id", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Name", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Serial", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Transport", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ConnectionState", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LastError", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Capabilities", type = LibPebble3StringList::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Watch1 : DBusInterface {
    fun Connect(): DBusPath
    fun Disconnect(): DBusPath
    fun Forget(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Firmware1")
@DBusProperties(
    DBusProperty(name = "FirmwareVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "LanguageVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Recovery", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "CheckingForUpdate", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "UpdateAvailable", type = Boolean::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "CandidateVersion", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ReleaseNotes", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "UpdateState", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "UpdateProgress", type = Double::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Firmware1 : DBusInterface {
    fun InstallFirmware(firmware: FileDescriptor): DBusPath
    fun InstallLanguagePack(languagePack: FileDescriptor): DBusPath
    fun CheckForUpdate(force: Boolean): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Applications1")
@DBusProperties(
    DBusProperty(name = "Applications", type = LibPebble3RecordList::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Applications1 : DBusInterface {
    fun Install(pbw: FileDescriptor): DBusPath
    fun Remove(uuid: String): DBusPath
    fun Launch(uuid: String): DBusPath
    fun Close(uuid: String): DBusPath
    fun RequestConfiguration(uuid: String): DBusPath
    fun SubmitConfiguration(uuid: String, result: String): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Timeline1")
@DBusProperties(
    DBusProperty(name = "CalendarEnabled", type = Boolean::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Timeline1 : DBusInterface {
    fun Sync(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Notifications1")
@DBusProperties(
    DBusProperty(name = "Filters", type = LibPebble3RecordList::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Notifications1 : DBusInterface {
    fun SetFilters(filters: List<Map<String, Variant<*>>>): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Messaging1")
@DBusProperties(
    DBusProperty(name = "CannedResponses", type = LibPebble3RecordList::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Favorites", type = LibPebble3RecordList::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Messaging1 : DBusInterface {
    fun SetCannedResponses(responses: List<Map<String, Variant<*>>>): DBusPath
    fun SetFavorites(favorites: List<Map<String, Variant<*>>>): DBusPath
    fun SendText(methodId: String, text: String): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Health1")
@DBusProperties(
    DBusProperty(name = "Settings", type = LibPebble3VariantMap::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Health1 : DBusInterface {
    fun SetSettings(settings: Map<String, Variant<*>>): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Profiles1")
@DBusProperties(
    DBusProperty(name = "Profiles", type = LibPebble3VariantMap::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Profiles1 : DBusInterface {
    fun SetProfiles(profiles: Map<String, Variant<*>>): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Screenshots1")
@DBusProperties(
    DBusProperty(name = "Screenshots", type = LibPebble3RecordList::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Screenshots1 : DBusInterface {
    fun Capture(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Logs1")
@DBusProperties(
    DBusProperty(
        name = "DumpPath",
        type = String::class,
        access = DBusProperty.Access.READ,
    ),
)
internal interface LibPebble3Logs1 : DBusInterface {
    /** Dumps only to ~/Downloads/pebble.log; it accepts no caller-controlled path. */
    fun Dump(): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Developer1")
@DBusProperties(
    DBusProperty(name = "LocalEnabled", type = Boolean::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Developer1 : DBusInterface {
    fun SetLocalEnabled(enabled: Boolean): DBusPath
}

@DBusInterfaceName("io.rebble.libpebble3.Operation1")
@DBusProperties(
    DBusProperty(name = "Kind", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "State", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Progress", type = Double::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Result", type = LibPebble3VariantMap::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "Error", type = String::class, access = DBusProperty.Access.READ),
    DBusProperty(name = "ErrorDetail", type = String::class, access = DBusProperty.Access.READ),
)
internal interface LibPebble3Operation1 : DBusInterface {
    fun Cancel()

    class Completed(path: String, success: Boolean) : DBusSignal(path, success)
}
