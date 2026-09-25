package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.LibPebbleConfig
import io.rebble.libpebblecommon.connection.ActiveDevice
import io.rebble.libpebblecommon.connection.BleDiscoveredPebbleDevice
import io.rebble.libpebblecommon.connection.BondedWatchImportOutcome
import io.rebble.libpebblecommon.connection.bt.ble.bluez.BluezManager
import io.rebble.libpebblecommon.connection.CommonConnectedDevice
import io.rebble.libpebblecommon.connection.DiscoveredPebbleDevice
import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckResult
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.PebbleBtClassicIdentifier
import io.rebble.libpebblecommon.connection.PebbleDevice
import io.rebble.libpebblecommon.database.dao.AppWithCount
import io.rebble.libpebblecommon.linux.weather.OpenMeteoWeatherClient
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.rockpool.SendTextConfigurationCoordinator
import io.rebble.libpebblecommon.util.GeolocationPositionResult
import io.rebble.libpebblecommon.util.SystemGeolocation
import io.rebble.libpebblecommon.rockpool.DevConnectionStateObserver
import io.rebble.libpebblecommon.rockpool.AccountSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.BondedWatchForgetCoordinator
import io.rebble.libpebblecommon.rockpool.BondedWatchForgetOutcome
import io.rebble.libpebblecommon.rockpool.NotificationFilterCoordinator
import io.rebble.libpebblecommon.rockpool.ProfileSettingsChange
import io.rebble.libpebblecommon.rockpool.ProfileSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.HealthSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.HealthSettingsUpdate
import io.rebble.libpebblecommon.rockpool.LibPebbleConfigMutationCoordinator
import io.rebble.libpebblecommon.rockpool.LibPebbleConfigUpdate
import io.rebble.libpebblecommon.rockpool.TimelineWindowCoordinator
import io.rebble.libpebblecommon.rockpool.loadPlatformNotificationFilters
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.freedesktop.dbus.DBusPath
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.Variant
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.time.Duration.Companion.seconds

internal data class RockpoolFirmwareStatus(
    val available: Boolean,
    val candidateVersion: String,
    val releaseNotes: String,
) {
    fun shouldSignalAfter(previous: RockpoolFirmwareStatus): Boolean = this != previous
}

private fun rockpoolFirmwareStatus(device: PebbleDevice?): RockpoolFirmwareStatus {
    val update = (device as? CommonConnectedDevice)?.firmwareUpdateAvailable?.result
        as? FirmwareUpdateCheckResult.FoundUpdate
    return RockpoolFirmwareStatus(
        available = update != null,
        candidateVersion = update?.version?.stringVersion.orEmpty(),
        releaseNotes = update?.notes.orEmpty(),
    )
}

/**
 * Claims org.rockpool on the session bus and maintains the Manager object plus one Pebble object
 * per known watch for the Rockpool Silica UI.
 */
internal class RockpoolUiService(
    private val libPebble: LibPebble,
    private val importBondedWatches:
        suspend (beginCommit: () -> Boolean) -> BondedWatchImportOutcome,
    private val bondedWatchForget: BondedWatchForgetCoordinator,
    private val settings: RockpoolSettings,
    private val notificationFilters: NotificationFilterCoordinator,
    private val accountSettings: AccountSettingsCoordinator,
    private val profileSettings: ProfileSettingsCoordinator,
    private val healthSettings: HealthSettingsCoordinator,
    private val timelineWindow: TimelineWindowCoordinator,
    private val classicAvailable: Boolean,
    private val configFitsStorage: (LibPebbleConfig) -> Boolean = { true },
    private val sendTextConfiguration: SendTextConfigurationCoordinator? = null,
) {
    private val logger = Logger.withTag("RockpoolUiService")
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val reconnecting = AtomicBoolean(false)
    private val observersStarted = AtomicBoolean(false)
    private val connectionLock = Any()
    private val weatherCoordinator = RockpoolWeatherCoordinator(settings, libPebble)
    // libpebble3 keeps health history per account, not per watch. Every exported legacy Pebble
    // object therefore shares this read-only projection and receives the same update signal.
    private val healthData = RockpoolHealthDataCoordinator(libPebble)
    private val weatherClient = OpenMeteoWeatherClient()
    private val weatherLocationNameResolver = GeoClueCurrentLocationNameResolver()
    private val weatherAutoRefresh = RockpoolWeatherAutoRefresh(
        scope = scope,
        coordinator = weatherCoordinator,
        units = { settings.get("weather.units", "m") },
        fetch = { _, coordinates, units ->
            weatherClient.fetch(
                latitude = coordinates.latitude,
                longitude = coordinates.longitude,
                imperial = units == "e",
            )?.toRockpoolWeatherObservation()
        },
        resolveCurrentLocation = {
            (libPebble.getCurrentPosition(
                maximumAge = SystemGeolocation.DEFAULT_MAX_AGE,
                timeout = SystemGeolocation.DEFAULT_TIMEOUT,
                // Sailfish maps a coarse request to non-satellite-only positioning. Request all
                // methods so BeaconDB/network fixes remain eligible while GPS can act as fallback.
                highAccuracy = true,
            ) as? GeolocationPositionResult.Success)?.let { position ->
                RockpoolWeatherCoordinates(
                    latitude = position.latitude,
                    longitude = position.longitude,
                    horizontalAccuracy = position.accuracy,
                )
            }
        },
        resolveCurrentLocationName = weatherLocationNameResolver::resolve,
        onFailure = { logger.w { "automatic weather refresh failed" } },
    )
    private val notificationAppearance = RockpoolNotificationAppearanceCoordinator(libPebble)
    private val configMutations = LibPebbleConfigMutationCoordinator.forLibPebble(libPebble)
    private val notificationFilterMutations = RockpoolNotificationFilterMutations(
        scope = scope,
        notificationFilters = notificationFilters,
        applicationName = { sourceId ->
            libPebble.notificationApps().first()
                .find { it.app.packageName == sourceId }
                ?.app
                ?.name
        },
    )

    @Volatile
    private var latestNotificationApps: List<AppWithCount> = emptyList()

    @Volatile
    private var connection: DBusConnection? = null
    private val exported = LinkedHashMap<String, ExportedWatch>() // address -> object
    private val notificationSourcePublisher = RockpoolNotificationSourcePublisher(
        snapshot = {
            rockpoolNotificationSources(
                latestNotificationApps,
                loadPlatformNotificationFilters(settings),
            )
        },
        emit = { source ->
            broadcastSignal { targetPath ->
                RockpoolPebble.NotificationFilterChanged(
                    targetPath,
                    source.sourceId,
                    source.name,
                    source.icon,
                    source.enabled,
                )
            }
        },
        onFailure = { error ->
            logger.w { "notification source publication failed: ${error.message}" }
        },
    )

    init {
        profileSettings.addListener(::profileSettingsChanged)
        healthSettings.addListener(::healthSettingsChanged)
        configMutations.addListener(::libPebbleConfigChanged)
        notificationFilters.addListener {
            notificationSourcePublisher.invalidate()
        }
    }

    private data class ExportedWatch(
        val path: String,
        val obj: RockpoolPebbleObject,
        val devConnectionObserver: DevConnectionStateObserver,
        var connected: Boolean,
        var connectionState: Int,
        var connectionError: String,
        var upgradingFirmware: Boolean = false,
        var firmwareStatus: RockpoolFirmwareStatus = RockpoolFirmwareStatus(false, "", ""),
        var languageStatus: RockpoolLanguageStatus = RockpoolLanguageStatus("", null),
    )

    @Volatile
    private var scanning = false

    @Volatile
    private var scanResults: List<Variant<*>> = emptyList()

    private val scanCandidatesLock = Any()
    private val scanCandidates =
        LinkedHashMap<String, RockpoolScanCandidate<DiscoveredPebbleDevice>>()

    // Addresses BlueZ has bonded, refreshed when a scan starts. Discovered watches whose address
    // is in here are already paired and are hidden from the scan results (see updateScanResults).
    @Volatile
    private var bondedAddresses: Set<String> = emptySet()

    private val mixedScanSession = RockpoolMixedScanSession(
        scope = scope,
        startBle = { libPebble.startBleScan() },
        stopBle = { libPebble.stopBleScan() },
        startClassic = { libPebble.startClassicScan() },
        stopClassic = { libPebble.stopClassicScan() },
        classicAvailable = classicAvailable,
        onStarted = {
            synchronized(scanCandidatesLock) { scanCandidates.clear() }
            publishScanResults(emptyList())
        },
        onActiveChanged = ::setScanning,
        onFailure = { operation, error -> logger.e(operation, error) },
    )

    private val scanCoordinator = RockpoolScanCoordinator(
        scope = scope,
        reconcileBondedWatches = ::reconcileBondedWatches,
        refreshBondedAddresses = { BluezManager.bondedAddresses() },
        applyBondedAddresses = { bondedAddresses = it },
        recomputeResults = { updateScanResults(libPebble.watches.value) },
        startScan = { mixedScanSession.start() },
        stopScan = { mixedScanSession.stop() },
        onReconcileFailure = { logger.e("StartScan bonded-watch import", it) },
        onRefreshFailure = { logger.e("StartScan bonded-address refresh", it) },
        onScanFailure = { operation, error -> logger.e(operation, error) },
    )

    private val connectCoordinator = RockpoolConnectCoordinator(scope) { address ->
        connectRockpoolWatch(address, libPebble.watches.value, ::connectCandidate)
    }

    private suspend fun reconcileBondedWatches(beginCommit: () -> Boolean) {
        when (val outcome = importBondedWatches(beginCommit)) {
            is BondedWatchImportOutcome.Completed -> {
                if (outcome.imported > 0) {
                    logger.i {
                        "StartScan imported ${outcome.imported} bonded watches " +
                            "(${outcome.existing} existing, ${outcome.aliases} aliases)"
                    }
                }
            }
            BondedWatchImportOutcome.Unavailable ->
                logger.w { "StartScan bonded-watch enumeration is unavailable" }
            BondedWatchImportOutcome.PersistenceFailed ->
                logger.w { "StartScan could not persist imported bonded watches" }
            BondedWatchImportOutcome.Cancelled -> Unit
        }
    }

    // Named class (not an anonymous object): dbus-java invokes exported objects reflectively,
    // so this needs a stable class name in the native-image reflection config.
    internal inner class ManagerObject : RockpoolManager {
        override fun getObjectPath() = MANAGER_PATH
        override fun isRemote() = false
        override fun Version(): String = VERSION
        override fun ListWatches(): List<DBusPath> =
            synchronized(exported) { exported.values.map { DBusPath(it.path) } }

        override fun StartScan() {
            // Refresh the bonded set first, so already-paired / dual-mode-misreporting watches
            // stay out of the fresh scan results (see updateScanResults).
            scanCoordinator.start()
        }

        override fun StopScan() {
            scanCoordinator.stop()
        }

        override fun IsScanning(): Boolean = scanning

        override fun ScanResults(): List<Variant<*>> {
            // Test hook: exercise the av/a{sv} marshalling + UI parsing without BT hardware.
            if (System.getenv("LIBPEBBLE3D_FAKE_SCAN") == "1") {
                return listOf(
                    scanResultVariant("Fake Pebble XYZW", "AA:BB:CC:DD:EE:FF", -42, "ble")
                )
            }
            return scanResults
        }

        override fun ConnectWatch(address: String) {
            if (!bondedWatchForget.connect(address)) {
                logger.w { "ConnectWatch: Forget is committing for $address" }
                return
            }
            scanCoordinator.stop()
            connectCoordinator.start(address)
        }

        override fun DisconnectWatch(address: String) {
            val device = findWatch("DisconnectWatch", address) ?: return
            val active = device as? ActiveDevice
            if (active == null) {
                logger.w {
                    "DisconnectWatch: $address is idle (${device::class.simpleName}); " +
                        "nothing to disconnect"
                }
                return
            }
            logger.i { "DisconnectWatch: disconnecting ${device.displayName()} ($address)" }
            active.disconnect()
        }

        override fun ForgetWatch(address: String) {
            val device = findWatch("ForgetWatch", address) ?: return
            val known = device as? KnownPebbleDevice
            if (known == null) {
                logger.w {
                    "ForgetWatch: $address was never paired (${device::class.simpleName})"
                }
                return
            }
            logger.i { "ForgetWatch: forgetting ${device.displayName()} ($address)" }
            scope.launch {
                when (
                    bondedWatchForget.forget(
                        address = address,
                        name = known.name,
                        beginCommit = { true },
                        disconnect = { (known as? ActiveDevice)?.disconnect() },
                        forgetPortable = known::forget,
                    )
                ) {
                    BondedWatchForgetOutcome.COMPLETED -> Unit
                    BondedWatchForgetOutcome.SUPERSEDED ->
                        logger.i { "ForgetWatch: reconnect superseded Forget for $address" }
                    else -> logger.w { "ForgetWatch: could not fully forget $address" }
                }
            }
        }

        private fun findWatch(caller: String, address: String): PebbleDevice? {
            val device = libPebble.watches.value.firstOrNull {
                it.identifier.asString.equals(address, ignoreCase = true)
            }
            if (device == null) logger.w { "$caller: $address not found" }
            return device
        }
    }

    private val manager = ManagerObject()

    fun start() {
        startObservers()
        ensureConnection()
    }

    /** Makes a delayed legacy import visible without requiring a daemon restart. */
    fun reloadWeatherSettings() {
        val changed = weatherCoordinator.reloadPersisted().getOrElse {
            logger.w { "persisted weather settings are invalid; keeping the live snapshot" }
            return
        }
        if (changed) weatherAutoRefresh.trigger()
    }

    private fun startObservers() {
        if (!observersStarted.compareAndSet(false, true)) return
        notificationSourcePublisher.start(scope)
        watchWatches()
        watchLocker()
        watchNotificationApps()
        watchHealthData()
        scope.launch {
            libPebble.watchPrefs.collect {
                broadcastSignal { path -> RockpoolPebble.QuietTimeSettingsChanged(path) }
            }
        }
        watchScanning()
        weatherAutoRefresh.start()
    }

    private fun ensureConnection() {
        if (!reconnecting.compareAndSet(false, true)) return
        scope.launch {
            try {
                while (connection == null) {
                    try {
                        connectAndExport()
                    } catch (e: Exception) {
                        logger.w { "org.rockpool unavailable (${e.message}); retrying in 30s" }
                        delay(30.seconds)
                    }
                }
            } finally {
                reconnecting.set(false)
                // The connection can disappear between the loop condition and
                // clearing this guard, so retain exactly one reconnect loop.
                if (connection == null) ensureConnection()
            }
        }
    }

    private suspend fun connectAndExport() = withContext(Dispatchers.IO) {
        // This adapter must not share the primary io.rebble.libpebble3 connection.  D-Bus exports
        // belong to a connection rather than a well-known name, so a shared connection would
        // accidentally expose new objects through org.rockpool (and vice versa).
        val conn = DBusConnectionBuilder.forSessionBus().withShared(false).build()
        try {
            // Fails while rockpoold still owns the name: intentional, rockpoold keeps priority.
            conn.requestBusName(BUS_NAME)
            conn.exportObject(MANAGER_PATH, manager)
            synchronized(exported) {
                exported.values.forEach { conn.exportObject(it.path, it.obj) }
                synchronized(connectionLock) { connection = conn }
            }
        } catch (e: Exception) {
            conn.disconnect()
            throw e
        }
        logger.i { "org.rockpool exported on the session bus" }
        watchBusConnection(conn)
    }

    /** dbus-java has no disconnect callback, so probe and re-export on loss. */
    private fun watchBusConnection(conn: DBusConnection) {
        scope.launch(Dispatchers.IO) {
            while (connection === conn) {
                delay(BUS_HEALTH_INTERVAL)
                if (connection !== conn) break
                val healthy = runCatching {
                    conn.getNames()
                    true
                }.getOrDefault(false)
                if (!healthy) {
                    markConnectionLost(conn)
                    break
                }
            }
        }
    }

    private fun markConnectionLost(conn: DBusConnection) {
        val detached = synchronized(connectionLock) {
            if (connection !== conn) false else {
                connection = null
                true
            }
        }
        if (!detached) return
        runCatching { conn.disconnect() }
        logger.w { "org.rockpool session-bus connection lost; reconnecting" }
        ensureConnection()
    }

    private fun watchScanning() = Unit

    private fun watchWatches() {
        scope.launch {
            libPebble.watches.collect { devices ->
                updateScanResults(devices)
                val known = devices.filterIsInstance<KnownPebbleDevice>()
                    .associateBy { it.identifier.asString.uppercase() }
                var listChanged = false
                synchronized(exported) {
                    (exported.keys - known.keys).toList().forEach { address ->
                        exported.remove(address)?.let { watch ->
                            watch.devConnectionObserver.close()
                            runCatching { connection?.unExportObject(watch.path) }
                            listChanged = true
                        }
                    }
                    known.forEach { (address, device) ->
                        val existing = exported[address]
                        // CommonConnectedDevice includes recovery-mode watches, which the UI
                        // must see as connected (recovery/firmware UI lives there).
                        val isConnected = device is CommonConnectedDevice
                        val state = connectionStateOf(device)
                        val error = device.connectionFailureInfo?.reason?.name ?: ""
                        if (existing == null) {
                            val path = "$PATH_PREFIX${address.replace(":", "_")}"
                            val obj = RockpoolPebbleObject(
                                address = address,
                                path = path,
                                libPebble = libPebble,
                                settings = settings,
                                notificationFilterMutations = notificationFilterMutations,
                                accountSettings = accountSettings,
                                profileSettings = profileSettings,
                                timelineWindow = timelineWindow,
                                healthCoordinator = healthSettings,
                                healthData = healthData,
                                weatherCoordinator = weatherCoordinator,
                                refreshWeather = weatherAutoRefresh::trigger,
                                notificationAppearance = notificationAppearance,
                                scope = scope,
                                emit = ::emitSignal,
                                broadcast = ::broadcastSignal,
                                configFitsStorage = configFitsStorage,
                                sendTextConfiguration = sendTextConfiguration,
                            )
                            runCatching { connection?.exportObject(path, obj) }
                                .onFailure { logger.e("export $path failed", it) }
                            val devConnectionObserver = DevConnectionStateObserver(scope) {
                                active -> emitSignal(
                                    RockpoolPebble.DevConnectionChanged(path, active)
                                )
                            }
                            exported[address] = ExportedWatch(
                                path,
                                obj,
                                devConnectionObserver,
                                isConnected,
                                state,
                                error,
                                languageStatus = rockpoolLanguageStatus(device),
                            )
                            listChanged = true
                            if (isConnected) emitSignal(RockpoolPebble.Connected(path))
                            if (state != RockpoolConnectionState.DISCONNECTED) {
                                emitSignal(RockpoolPebble.ConnectionStateChanged(path, state))
                            }
                        } else {
                            if (existing.connected != isConnected) {
                                existing.connected = isConnected
                                emitSignal(
                                    if (isConnected) RockpoolPebble.Connected(existing.path)
                                    else RockpoolPebble.Disconnected(existing.path)
                                )
                            }
                            // Emit the finer-grained state too (connecting/negotiating/failed),
                            // which the connected/disconnected pair can't express — this is what
                            // lets PairWatchPage show progress and stop spinning on a failed pair.
                            if (shouldEmitConnectionStateChanged(
                                existing.connectionState,
                                existing.connectionError,
                                state,
                                error,
                            )) {
                                existing.connectionState = state
                                existing.connectionError = error
                                emitSignal(RockpoolPebble.ConnectionStateChanged(existing.path, state))
                            }
                        }
                        exported[address]?.let { watch ->
                            val common = device as? CommonConnectedDevice
                            watch.devConnectionObserver.update(
                                source = common,
                                state = common?.devConnectionActive,
                            )
                            val upgrading = (common?.firmwareUpdateState
                                is FirmwareUpdater.FirmwareUpdateStatus.Active)
                            if (watch.upgradingFirmware != upgrading) {
                                watch.upgradingFirmware = upgrading
                                emitSignal(RockpoolPebble.UpgradingFirmwareChanged(watch.path))
                            }
                            val firmwareStatus = rockpoolFirmwareStatus(device)
                            if (firmwareStatus.shouldSignalAfter(watch.firmwareStatus)) {
                                watch.firmwareStatus = firmwareStatus
                                emitSignal(
                                    RockpoolPebble.FirmwareUpgradeAvailableChanged(watch.path)
                                )
                            }
                            val languageStatus = rockpoolLanguageStatus(device)
                            if (languageStatus.shouldSignalAfter(watch.languageStatus)) {
                                emitSignal(RockpoolPebble.LanguageVersionChanged(watch.path))
                            }
                            watch.languageStatus = languageStatus
                        }
                    }
                }
                if (listChanged) emitSignal(RockpoolManager.PebblesChanged(MANAGER_PATH))
            }
        }
    }

    private fun updateScanResults(devices: List<io.rebble.libpebblecommon.connection.PebbleDevice>) {
        // Watches we already know (paired via the UI or currently connected) or that BlueZ still
        // has bonded shouldn't reappear as "new" watches to pair. Dual-mode watches like
        // obelix/getafix misreport BR/EDR and linger as bonded BlueZ objects, so without this they
        // show up on every scan despite being set up already; the bonded set covers those and any
        // other already-paired watch.
        val hidden = devices.filterIsInstance<KnownPebbleDevice>()
            .mapTo(HashSet()) { it.identifier.asString.uppercase() }
            .apply { addAll(bondedAddresses) }
        val accumulating = mixedScanSession.isActive()
        val results = synchronized(scanCandidatesLock) {
            val discoveries = if (accumulating) {
                devices.filterIsInstance<DiscoveredPebbleDevice>().map(::scanCandidateOf)
            } else {
                emptyList()
            }
            val updated = RockpoolScanCandidatePolicy.merge(scanCandidates, hidden, discoveries)
            scanCandidates.clear()
            scanCandidates.putAll(updated)
            scanCandidates.values.map { candidate ->
                val device = candidate.device
                scanResultVariant(
                    name = device.displayName(),
                    address = device.identifier.asString,
                    rssi = (device as? BleDiscoveredPebbleDevice)?.rssi ?: 0,
                    transport = candidate.transport.dbusName,
                )
            }
        }
        publishScanResults(results)
    }

    private suspend fun connectCandidate(address: String) {
        val key = address.uppercase()
        val remembered = synchronized(scanCandidatesLock) { scanCandidates[key] }
        val transport = remembered?.transport
        val device = try {
            var current = currentDiscoveredCandidate(key, transport)
            if (current == null && remembered != null) {
                if (transport == RockpoolScanTransport.CLASSIC && !classicAvailable) {
                    logger.w { "ConnectWatch: Classic transport is unavailable for $address" }
                    return
                }
                if (transport == RockpoolScanTransport.CLASSIC) {
                    libPebble.startClassicScan()
                } else {
                    libPebble.startBleScan()
                }
                current = withTimeoutOrNull(CANDIDATE_REDISCOVERY_TIMEOUT) {
                    libPebble.watches.mapNotNull { watches ->
                        watches.filterIsInstance<DiscoveredPebbleDevice>()
                            .map(::scanCandidateOf)
                            .firstOrNull { RockpoolScanCandidatePolicy.matches(it, key, transport) }
                            ?.device
                    }.first()
                }
            }
            current
        } finally {
            libPebble.stopBleScan()
            libPebble.stopClassicScan()
        }
        if (device == null) {
            logger.w { "ConnectWatch: $address is no longer visible" }
            return
        }
        logger.i { "ConnectWatch: connecting to ${device.displayName()} ($address)" }
        device.connect()
    }

    private fun currentDiscoveredCandidate(
        normalizedAddress: String,
        transport: RockpoolScanTransport?,
    ): DiscoveredPebbleDevice? = libPebble.watches.value
        .filterIsInstance<DiscoveredPebbleDevice>()
        .map(::scanCandidateOf)
        .firstOrNull { RockpoolScanCandidatePolicy.matches(it, normalizedAddress, transport) }
        ?.device

    private fun scanCandidateOf(device: DiscoveredPebbleDevice) = RockpoolScanCandidate(
        normalizedAddress = device.identifier.asString.uppercase(),
        device = device,
        transport = transportOf(device),
    )

    private fun transportOf(device: PebbleDevice): RockpoolScanTransport =
        if (device.identifier is PebbleBtClassicIdentifier) {
            RockpoolScanTransport.CLASSIC
        } else {
            RockpoolScanTransport.BLE
        }

    private fun setScanning(value: Boolean) {
        if (scanning == value) return
        scanning = value
        emitSignal(RockpoolManager.ScanningChanged(MANAGER_PATH, value))
    }

    private fun publishScanResults(results: List<Variant<*>>) {
        if (scanResults == results) return
        scanResults = results
        emitSignal(RockpoolManager.ScanResultsChanged(MANAGER_PATH))
    }

    private fun watchLocker() {
        val storeAssociations = RockpoolStoreAssociations(libPebble, changed = {
            broadcastSignal { path -> RockpoolPebble.InstalledAppsChanged(path) }
        })
        scope.launch {
            libPebble.getAllLockerUuids().distinctUntilChanged().collect {
                storeAssociations.refresh(it)
            }
        }
        scope.launch {
            libPebble.getAllLockerUuids().distinctUntilChanged().drop(1).collect {
                synchronized(exported) { exported.values.map { it.path } }
                    .forEach { emitSignal(RockpoolPebble.InstalledAppsChanged(it)) }
            }
        }
    }

    private fun watchNotificationApps() {
        scope.launch {
            libPebble.notificationApps().collect { apps ->
                latestNotificationApps = apps
                notificationSourcePublisher.invalidate()
            }
        }
    }

    private fun profileSettingsChanged(change: ProfileSettingsChange) {
        val path = synchronized(exported) { exported[change.address]?.path } ?: return
        if ("connected" in change.kinds) {
            emitSignal(RockpoolPebble.ProfileWhenConnectedChanged(path))
        }
        if ("disconnected" in change.kinds) {
            emitSignal(RockpoolPebble.ProfileWhenDisconnectedChanged(path))
        }
    }

    private fun healthSettingsChanged(change: HealthSettingsUpdate) {
        if (rockpoolHealthParams(change.previous) != rockpoolHealthParams(change.current)) {
            broadcastSignal { targetPath -> RockpoolPebble.HealthParamsChanged(targetPath) }
        }
        if (change.previous.imperialUnits != change.current.imperialUnits) {
            broadcastSignal { targetPath -> RockpoolPebble.ImperialUnitsChanged(targetPath) }
        }
    }

    private fun watchHealthData() {
        scope.launch {
            libPebble.healthDataUpdated.collect {
                broadcastSignal { targetPath -> RockpoolPebble.HealthDataChanged(targetPath) }
            }
        }
    }

    private fun libPebbleConfigChanged(change: LibPebbleConfigUpdate) {
        if (change.previous.watchConfig.enableWatchSettingsSync != change.current.watchConfig.enableWatchSettingsSync) {
            broadcastSignal { path -> RockpoolPebble.QuietTimeSettingsChanged(path) }
        }
        if (change.previous.watchConfig.calendarPins != change.current.watchConfig.calendarPins) {
            broadcastSignal { targetPath ->
                RockpoolPebble.CalendarSyncEnabledChanged(targetPath)
            }
        }
    }

    private fun broadcastSignal(signalForPath: (String) -> DBusSignal) {
        synchronized(exported) { exported.values.map { it.path } }
            .forEach { emitSignal(signalForPath(it)) }
    }

    private fun emitSignal(signal: DBusSignal) {
        val conn = connection ?: return
        runCatching { conn.sendMessage(signal) }
            .onFailure {
                logger.w { "signal emit failed: ${it.message}" }
                markConnectionLost(conn)
            }
    }

    companion object {
        private const val BUS_NAME = "org.rockpool"
        private const val MANAGER_PATH = "/org/rockpool/Manager"
        private const val PATH_PREFIX = "/org/rockpool/"
        private const val VERSION = "2.0.0-libpebble3d"
        private val CANDIDATE_REDISCOVERY_TIMEOUT = 15.seconds
        private val BUS_HEALTH_INTERVAL = 10.seconds

        private fun scanResultVariant(
            name: String,
            address: String,
            rssi: Int,
            transport: String,
        ): Variant<*> =
            Variant(
                mapOf(
                    "name" to Variant(name),
                    "address" to Variant(address),
                    "rssi" to Variant(rssi),
                    "transport" to Variant(transport),
                ),
                "a{sv}",
            )
    }
}

internal fun shouldEmitConnectionStateChanged(
    previousState: Int,
    previousError: String,
    state: Int,
    error: String,
): Boolean = previousState != state || previousError != error
