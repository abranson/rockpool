package io.rebble.libpebblecommon.ui

import org.freedesktop.dbus.DBusPath
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.UInt16
import org.freedesktop.dbus.types.Variant

/**
 * The private org.rockpool session-bus facade consumed by the Silica UI.
 * Method names/casing must match exactly: QDBusInterface calls are name-based.
 */
@DBusInterfaceName("org.rockpool.Manager")
interface RockpoolManager : DBusInterface {
    fun Version(): String
    fun ListWatches(): List<DBusPath>

    // BLE pairing has to go through the daemon (JustWorks agent + PPoGATT), not the
    // system Bluetooth settings — these back the UI's pairing page. rockpoold never
    // had them; they are additions to the org.rockpool contract.
    fun StartScan()
    fun StopScan()
    fun IsScanning(): Boolean

    // 'av' with each variant wrapping a{sv} — NOT aa{sv}. The UI's demarshalling depends on
    // this exact historical shape.
    fun ScanResults(): List<Variant<*>>
    fun ConnectWatch(address: String)

    /**
     * Stop reconnecting, keeping the watch known. Only possible while it is connected or
     * attempting to connect — libpebble3 exposes this through ActiveDevice, which an idle known
     * watch is not. Use [ForgetWatch] to clear one of those.
     */
    fun DisconnectWatch(address: String)

    /**
     * Forget the watch: disconnect, drop it from the known list, purge its sync records.
     *
     * Without this the UI could only ever ratchet one way — ConnectWatch persists a connect goal
     * that survives restarts and retries forever, and nothing could clear it.
     */
    fun ForgetWatch(address: String)

    class PebblesChanged(path: String) : DBusSignal(path)
    class ScanningChanged(path: String, scanning: Boolean) : DBusSignal(path, scanning)
    class ScanResultsChanged(path: String) : DBusSignal(path)
}

@DBusInterfaceName("org.rockpool.Pebble")
interface RockpoolPebble : DBusInterface {
    // Identity / hardware
    fun Address(): String
    fun Name(): String
    fun SerialNumber(): String
    fun PlatformString(): String
    fun HardwarePlatform(): String
    fun SoftwareVersion(): String
    fun LanguageVersion(): String
    fun Model(): Int
    fun IsConnected(): Boolean
    // Full connection state (see RockpoolConnectionState), replacing the lossy IsConnected bool:
    // a watch stuck reconnecting or one that just failed to pair is otherwise indistinguishable
    // from an idle disconnected watch, so PairWatchPage spins forever with no failure to show.
    fun ConnectionState(): Int
    // The last connection-failure reason (ConnectionFailureReason name), empty once connected.
    fun LastError(): String
    fun Recovery(): Boolean

    // Firmware / language packs
    fun FirmwareUpgradeAvailable(): Boolean
    fun CandidateFirmwareVersion(): String
    fun FirmwareReleaseNotes(): String
    fun PerformFirmwareUpgrade()
    fun UpgradingFirmware(): Boolean
    fun LoadLanguagePack(pblFile: String)

    // Account / cloud sync
    fun accountName(): String
    fun accountEmail(): String
    fun HasOAuthToken(): Boolean
    fun setOAuthToken(token: String)
    fun syncAppsFromCloud(): Boolean
    fun setSyncAppsFromCloud(enable: Boolean)
    fun resetTimeline()

    // Shared watch preferences (synchronized by libpebble3).
    fun QuietTimeSettings(): Map<String, Variant<*>>
    fun SetQuietTimeSetting(key: String, value: String): Boolean

    // Timeline
    fun setTimelineWindow(start: Int, fade: Int, end: Int)
    fun timelineWindowStart(): Int
    fun timelineWindowFade(): Int
    fun timelineWindowEnd(): Int
    fun insertTimelinePin(jsonPin: String)

    // Notification filter
    fun NotificationsFilter(): Map<String, Variant<*>>
    fun SetNotificationFilter(sourceId: String, enabled: Int)
    fun ForgetNotificationFilter(sourceId: String)
    // Per-app notification appearance override; empty value clears it. iconCode is a
    // TimelineIcon.code, colorName a TimelineColor.name (both readable via NotificationsFilter).
    fun SetNotificationAppColor(sourceId: String, colorName: String)
    fun SetNotificationAppIcon(sourceId: String, iconCode: String)
    // Constant TimelineColor / TimelineIcon palettes backing the appearance pickers. Global data,
    // but exposed on the Pebble object because the notifications page only holds a Pebble handle.
    // Each entry is an a{sv}: colours {name, displayName, rgb}, icons {code, name}. Sourced from
    // libpebble3 so the UI stays in sync as the icon set grows.
    fun TimelineColors(): List<Variant<*>>
    fun TimelineIcons(): List<Variant<*>>

    // Canned responses / favorite contacts
    fun cannedResponses(): Map<String, Variant<*>>
    fun setCannedResponses(cans: Map<String, Variant<*>>)
    fun getCannedResponses(groups: List<String>): Map<String, Variant<*>>
    fun setFavoriteContacts(cans: Map<String, Variant<*>>)
    fun getFavoriteContacts(names: List<String>): Map<String, Variant<*>>

    // Voice
    fun voiceSessionResult(dumpFile: String, sentences: List<Variant<*>>)

    // Developer connection / logging
    fun DevConnectionEnabled(): Boolean
    fun DevConnListenPort(): UInt16
    fun DevConnectionState(): Boolean
    fun DevConnCloudEnabled(): Boolean
    fun DevConnCloudState(): Boolean
    fun SetDevConnEnabled(enabled: Boolean)
    fun SetDevConnCloudEnabled(enabled: Boolean)
    fun SetDevConnListenPort(port: UInt16)
    fun startLogDump(): String
    fun stopLogDump(): String
    fun getLogDump(): String
    fun isLogDumping(): Boolean
    fun setLogLevel(level: Int)
    fun getLogLevel(): Int
    fun DumpLogs(fileName: String)

    // Apps / watchfaces
    fun InstallApp(id: String)
    fun SideloadApp(packageFile: String)
    fun InstalledAppIds(): List<String>

    // 'av' like rockpoold's QVariantList, see RockpoolManager.ScanResults.
    fun InstalledApps(): List<Variant<*>>
    fun RemoveApp(id: String)
    fun ConfigurationURL(uuid: String)
    fun ConfigurationClosed(uuid: String, result: String)
    fun SetAppOrder(newList: List<String>)
    fun SendAppData(uuid: String, data: Map<String, Variant<*>>)
    fun CloseApp(uuid: String)
    fun LaunchApp(uuid: String)

    // Screenshots
    fun RequestScreenshot()
    fun Screenshots(): List<String>
    fun RemoveScreenshot(filename: String)

    // Weather
    fun setWeatherApiKey(key: String)
    fun WeatherUnits(): String
    fun setWeatherUnits(units: String)
    fun WeatherLanguage(): String
    fun setWeatherLanguage(lang: String)
    fun WeatherAltKey(): String
    fun setWeatherAltKey(key: String)
    fun WeatherLocations(): List<Variant<*>>
    fun SetWeatherLocations(locations: List<Variant<*>>)
    fun InjectWeatherData(locationName: String, conditions: Map<String, Variant<*>>)

    // Health / units / profiles / calendar
    fun HealthParams(): Map<String, Variant<*>>
    fun SetHealthParams(params: Map<String, Variant<*>>)
    // Historical account-global health history API. Keep this on org.rockpool only; the
    // public io.rebble.libpebble3 XML intentionally has no legacy health-history surface.
    fun HealthOverview(): Map<String, Variant<*>>
    fun FetchHealthData()
    fun ImperialUnits(): Boolean
    fun SetImperialUnits(imperial: Boolean)
    fun ProfileWhenConnected(): String
    fun SetProfileWhenConnected(profile: String)
    fun ProfileWhenDisconnected(): String
    fun SetProfileWhenDisconnected(profile: String)
    fun CalendarSyncEnabled(): Boolean
    fun SetCalendarSyncEnabled(enabled: Boolean)

    class Connected(path: String) : DBusSignal(path)
    class Disconnected(path: String) : DBusSignal(path)
    class ConnectionStateChanged(path: String, state: Int) : DBusSignal(path, state)
    class InstalledAppsChanged(path: String) : DBusSignal(path)

    // Legacy QVariantList: av with each entry wrapping one as [name, latitude, longitude].
    class WeatherLocationsChanged(
        path: String,
        locations: List<Variant<*>>,
    ) : DBusSignal(path, locations)

    // ConfigurationURL is the legacy void call; the UI opens whatever URL this signal emits.
    class OpenURL(path: String, uuid: String, url: String) : DBusSignal(path, uuid, url)

    class NotificationFilterChanged(
        path: String, sourceId: String, name: String, icon: String, enabled: Int,
    ) : DBusSignal(path, sourceId, name, icon, enabled)
    class ScreenshotAdded(path: String, filename: String) : DBusSignal(path, filename)
    class ScreenshotRemoved(path: String, filename: String) : DBusSignal(path, filename)
    class FirmwareUpgradeAvailableChanged(path: String) : DBusSignal(path)
    class UpgradingFirmwareChanged(path: String) : DBusSignal(path)
    class LanguageVersionChanged(path: String) : DBusSignal(path)
    class LogsDumped(path: String, success: Boolean) : DBusSignal(path, success)
    class QuietTimeSettingsChanged(path: String) : DBusSignal(path)
    class CalendarSyncEnabledChanged(path: String) : DBusSignal(path)
    class ImperialUnitsChanged(path: String) : DBusSignal(path)
    class ProfileWhenConnectedChanged(path: String) : DBusSignal(path)
    class ProfileWhenDisconnectedChanged(path: String) : DBusSignal(path)
    class HealthParamsChanged(path: String) : DBusSignal(path)
    class HealthDataChanged(path: String) : DBusSignal(path)
    class DevConnectionChanged(path: String, state: Boolean) : DBusSignal(path, state)
    class DevConnCloudChanged(path: String, state: Boolean) : DBusSignal(path, state)
}

/**
 * org.rockpool.Pebble.ConnectionState() codes. Ordered so a plain > comparison means "more
 * connected". Distinguishes the states the old IsConnected() bool collapsed together.
 */
object RockpoolConnectionState {
    const val DISCONNECTED = 0
    const val CONNECTING = 1
    const val NEGOTIATING = 2
    const val CONNECTED = 3
    const val FAILED = 4
}

/** Derive a [RockpoolConnectionState] code from a libpebble3 device (null = not in the list). */
internal fun connectionStateOf(
    device: io.rebble.libpebblecommon.connection.PebbleDevice?,
): Int = when {
    device is io.rebble.libpebblecommon.connection.CommonConnectedDevice ->
        RockpoolConnectionState.CONNECTED
    device is io.rebble.libpebblecommon.connection.ConnectingPebbleDevice ->
        if (device.negotiating) RockpoolConnectionState.NEGOTIATING
        else RockpoolConnectionState.CONNECTING
    device?.connectionFailureInfo != null -> RockpoolConnectionState.FAILED
    else -> RockpoolConnectionState.DISCONNECTED
}
