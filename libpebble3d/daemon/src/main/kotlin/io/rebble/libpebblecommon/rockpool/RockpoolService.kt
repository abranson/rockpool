/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.ActiveDevice
import io.rebble.libpebblecommon.connection.BleDiscoveredPebbleDevice
import io.rebble.libpebblecommon.connection.BondedWatchImportOutcome
import io.rebble.libpebblecommon.connection.CommonConnectedDevice
import io.rebble.libpebblecommon.connection.ConnectionFailureInfo
import io.rebble.libpebblecommon.connection.ConnectingPebbleDevice
import io.rebble.libpebblecommon.connection.ConnectedPebbleDevice
import io.rebble.libpebblecommon.connection.DiscoveredPebbleDevice
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.PebbleBtClassicIdentifier
import io.rebble.libpebblecommon.connection.PebbleDevice
import io.rebble.libpebblecommon.connection.PebbleIdentifier
import io.rebble.libpebblecommon.connection.bt.ble.bluez.BluezManager
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.retryWhen
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.freedesktop.dbus.DBusPath
import org.freedesktop.dbus.FileDescriptor
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.exceptions.DBusExecutionException
import org.freedesktop.dbus.interfaces.ObjectManager
import org.freedesktop.dbus.interfaces.Properties
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import java.io.File
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.time.Duration.Companion.seconds
import kotlin.uuid.Uuid

internal fun primaryWatchCapabilities(): List<String> = listOf(
    "watch.notifications",
    "watch.messaging",
    "watch.health",
    "watch.screenshots",
    "watch.logs",
    "watch.developer-mode",
)

internal fun connectionAttemptIsTerminal(
    isConnected: Boolean,
    initialFailure: ConnectionFailureInfo?,
    currentFailure: ConnectionFailureInfo?,
): Boolean = isConnected || (currentFailure != null && currentFailure != initialFailure)

/**
 * The primary, versioned D-Bus surface.  It intentionally has no dependency on
 * the one-release org.rockwork adapter other than the shared settings backend.
 * Each service owns a distinct non-shared session connection so an exported
 * object cannot leak through the other well-known name.
 */
internal class RockpoolService(
    private val libPebble: LibPebble,
    private val requestConnectionImmediately: (PebbleIdentifier) -> Unit,
    private val cancelConnectionImmediately: (PebbleIdentifier) -> Unit,
    private val awaitConnectionRetired: suspend (PebbleIdentifier) -> Unit,
    private val importBondedWatches: suspend (beginCommit: () -> Boolean) -> BondedWatchImportOutcome,
    private val bondedWatchForget: BondedWatchForgetCoordinator,
    private val settings: RockpoolSettings,
    private val platformProvider: PlatformProviderController,
    private val notificationFilters: NotificationFilterCoordinator,
    private val accountSettings: AccountSettingsCoordinator,
    private val profileSettings: ProfileSettingsCoordinator,
    private val healthSettings: HealthSettingsCoordinator,
    private val cannedResponsesReconciler: PrimaryCannedResponsesReconciler,
    private val classicAvailable: Boolean,
) {
    private val logger = Logger.withTag("RockpoolService")
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val objectsLock = Any()
    private val connectionLock = Any()
    private val primaryConnectionAttempts = PrimaryConnectionAttemptRegistry()
    private val reconnecting = AtomicBoolean(false)
    private val observersStarted = AtomicBoolean(false)
    private val configMutations = LibPebbleConfigMutationCoordinator.forLibPebble(libPebble)

    init {
        accountSettings.addListener(::accountSessionChanged)
        accountSettings.addIdentityListener { accountIdentityChanged() }
        profileSettings.addListener(::profileSettingsChanged)
        healthSettings.addListener { healthSettingsPropertiesChanged() }
        configMutations.addListener(::libPebbleConfigChanged)
    }

    @Volatile
    private var connection: DBusConnection? = null

    private val managedObjects = ManagedObjectPublisher<DBusConnection>(
        objectsLock = objectsLock,
        connectionLock = connectionLock,
        currentConnection = { connection },
        setConnection = { connection = it },
    )

    @Volatile
    private var scanning = false

    @Volatile
    private var scanResults: List<Map<String, Variant<*>>> = emptyList()

    @Volatile
    private var bondedAddresses: Set<String> = emptySet()

    private val accountSyncCoordinator = AccountSyncCoordinator {
        manager.propertiesChanged(ACCOUNT_INTERFACE, setOf("SyncState"))
    }

    @Volatile
    private var lockerApplications: List<LockerWrapper> = emptyList()

    @Volatile
    private var screenshots: List<RockpoolScreenshotRecord> = emptyList()

    private val screenshotStore by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        RockpoolScreenshotStore.forCurrentUser()
    }
    private val screenshotStoreMutex = Mutex()

    private val watches = LinkedHashMap<String, ExportedWatch>()
    private val operations = LinkedHashMap<String, RockpoolOperationObject>()

    private data class ExportedWatch(
        val path: String,
        val objectId: String,
        val watch: RockpoolWatchObject,
        val devConnectionObserver: DevConnectionStateObserver,
    )

    private data class PlatformDomain(
        val bit: Long,
        val healthName: String,
        val capability: String,
    )

    private val root = RootObject()
    private val manager = ManagerObject()
    private val platform = PlatformObject()

    init {
        notificationFilters.addListener(::notificationFilterPropertiesChanged)
    }

    fun start() {
        startObservers()
        ensureConnection()
    }

    private fun startObservers() {
        if (!observersStarted.compareAndSet(false, true)) return
        watchScanning()
        watchApplications()
        watchScreenshots()
        watchWatches()
        platformProvider.addListener {
            platform.propertiesChanged(
                PLATFORM_INTERFACE,
                platform.managedInterfaces()[PLATFORM_INTERFACE]?.keys.orEmpty(),
            )
        }
    }

    private fun ensureConnection() {
        if (!reconnecting.compareAndSet(false, true)) return
        scope.launch {
            try {
                while (connection == null) {
                    try {
                        connectAndExport()
                    } catch (e: Exception) {
                        logger.w { "org.rockpool unavailable (${e.message}); retrying in 5s" }
                        delay(5.seconds)
                    }
                }
            } finally {
                reconnecting.set(false)
                // A connection can disappear between the loop condition and this point.
                if (connection == null) ensureConnection()
            }
        }
    }

    private suspend fun connectAndExport() = withContext(Dispatchers.IO) {
        // withShared(false) is mandatory.  dbus-java otherwise caches one physical
        // connection, exposing all objects through both org.rockpool and org.rockwork.
        val conn = DBusConnectionBuilder.forSessionBus().withShared(false).build()
        try {
            managedObjects.publishConnectionAfterExport(
                connection = conn,
                publishPublicName = { it.requestBusName(BUS_NAME) },
            ) { snapshotConnection ->
                snapshotConnection.exportObject(ROOT_PATH, root)
                snapshotConnection.exportObject(MANAGER_PATH, manager)
                snapshotConnection.exportObject(PLATFORM_PATH, platform)
                watches.values.forEach { snapshotConnection.exportObject(it.path, it.watch) }
                operations.values.forEach { snapshotConnection.exportObject(it.path, it) }
            }
        } catch (e: Exception) {
            conn.disconnect()
            throw e
        }

        logger.i { "org.rockpool exported on an isolated session-bus connection" }
        watchBusConnection(conn)
    }

    /** dbus-java has no disconnect callback; probe the physical connection and re-export on loss. */
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
        finishConnectionLoss(conn)
    }

    private fun finishConnectionLoss(conn: DBusConnection) {
        runCatching { conn.disconnect() }
        logger.w { "org.rockpool session-bus connection lost; reconnecting" }
        ensureConnection()
    }

    private fun watchScanning() {
        scope.launch {
            combine(
                libPebble.isScanningBle,
                libPebble.isScanningClassic,
            ) { ble, classic -> primaryScanning(ble, classic) }
                .distinctUntilChanged()
                .collect { value: Boolean ->
                if (scanning != value) {
                    scanning = value
                    manager.propertiesChanged(DISCOVERY_INTERFACE, setOf("Scanning"))
                }
            }
        }
    }

    private fun watchApplications() {
        scope.launch {
            combine(
                libPebble.getLocker(AppType.Watchface, null, LOCKER_LIMIT),
                libPebble.getLocker(AppType.Watchapp, null, LOCKER_LIMIT),
            ) { watchfaces, watchapps ->
                watchfaces + watchapps
            }.distinctUntilChanged()
                .retryWhen { cause, _ ->
                    if (cause is CancellationException) return@retryWhen false
                    logger.e("application locker observer failed; retrying", cause)
                    delay(LOCKER_RETRY_DELAY)
                    true
                }
                .collect { applications ->
                    lockerApplications = applications
                    applicationPropertiesChanged()
                }
        }
    }

    private fun watchScreenshots() {
        scope.launch(Dispatchers.IO) {
            reloadScreenshots()
        }
    }

    private fun profileSettingsChanged(change: ProfileSettingsChange) {
        val watch = synchronized(objectsLock) {
            watches.values.firstOrNull { it.watch.matchesAddress(change.address) }
        }
        watch?.watch?.propertiesChanged(PROFILES_INTERFACE, setOf("Profiles"))
    }

    private fun accountSessionChanged(change: AccountSessionChange) {
        // The Room observer will repopulate this from the replacement account. Clear the cached
        // projection synchronously so Account1 cannot publish new credentials alongside the old
        // account's application metadata.
        lockerApplications = emptyList()
        accountSyncCoordinator.accountChanged(change.generation)
        manager.propertiesChanged(
            ACCOUNT_INTERFACE,
            setOf("Authenticated", "Name", "Email", "SyncState"),
        )
        applicationPropertiesChanged()
        if (change.token.isNotBlank()) {
            scope.launch {
                val result = accountSyncCoordinator.synchronize(
                    beginCommit = { true },
                    start = libPebble::requestLockerSync,
                )
                if (result is AccountSyncResult.Failed) {
                    logger.w(result.cause) {
                        "automatic account locker synchronization failed"
                    }
                }
            }
        }
    }

    private fun accountIdentityChanged() {
        manager.propertiesChanged(ACCOUNT_INTERFACE, setOf("Name", "Email"))
    }

    private suspend fun reloadScreenshots() {
        val changed = try {
            screenshotStoreMutex.withLock {
                val loaded = screenshotStore.list()
                if (loaded == screenshots) {
                    false
                } else {
                    screenshots = loaded
                    true
                }
            }
        } catch (e: Exception) {
            logger.w(e) { "could not load saved screenshots" }
            return
        }
        if (changed) screenshotPropertiesChanged()
    }

    private fun watchWatches() {
        scope.launch {
            libPebble.watches.collect { devices ->
                updateScanResults(devices)
                val known = devices.filterIsInstance<KnownPebbleDevice>()
                    .associateBy { watchObjectId(it) }

                val removed = mutableListOf<ExportedWatch>()
                val added = mutableListOf<ExportedWatch>()
                val publication = managedObjects.mutate {
                    (watches.keys - known.keys).forEach { id ->
                        val exported = watches[id] ?: return@forEach
                        remove(
                            unexport = { it.unExportObject(exported.path) },
                            hide = { watches.remove(id) },
                        )
                        removed += exported
                    }
                    known.forEach { (id, device) ->
                        val current = watches[id]
                        if (current == null) {
                            val path = "$WATCHES_PATH/w_$id"
                            val obj = RockpoolWatchObject(id, path, device.identifier.asString)
                            val observer = DevConnectionStateObserver(scope) {
                                obj.propertiesChanged(DEVELOPER_INTERFACE, setOf("LocalEnabled"))
                            }
                            val exported = ExportedWatch(path, id, obj, observer)
                            publish(
                                export = { it.exportObject(exported.path, exported.watch) },
                                makeVisible = { watches[id] = exported },
                            )
                            added += exported
                        } else {
                            current.watch.updateAddress(device.identifier.asString)
                        }
                    }
                }

                publication.failure?.let {
                    logger.e("could not update primary watch objects; reconnecting", it)
                }
                publication.lostConnection?.let(::finishConnectionLoss)
                removed.forEach { exported ->
                    exported.devConnectionObserver.close()
                    emitRemoved(
                        publication.connection,
                        exported.path,
                        exported.watch.managedInterfaces().keys,
                    )
                }
                added.forEach { exported ->
                    emitAdded(
                        publication.connection,
                        exported.path,
                        exported.watch.managedInterfaces(),
                    )
                }

                synchronized(objectsLock) { watches.values.toList() }
                    .forEach { exported ->
                        val common = known[exported.objectId] as? CommonConnectedDevice
                        exported.devConnectionObserver.update(
                            source = common,
                            state = common?.devConnectionActive,
                        )
                        exported.watch.propertiesChangedAll()
                    }
                manager.propertiesChanged(MANAGER_INTERFACE, setOf("Watches", "State"))
            }
        }
    }

    private fun updateScanResults(devices: List<PebbleDevice>) {
        val knownAddresses = devices.filterIsInstance<KnownPebbleDevice>()
            .mapTo(HashSet()) { it.identifier.asString.uppercase() }
            .apply { addAll(bondedAddresses) }
        val candidates = devices.filterIsInstance<DiscoveredPebbleDevice>()
            .filter { it.identifier.asString.uppercase() !in knownAddresses }
            .associateBy { it.identifier.asString.uppercase() }
            .values
            .map { candidate ->
                record(
                    "name" to Variant(candidate.displayName()),
                    "address" to Variant(candidate.identifier.asString),
                    "transport" to Variant(transportOf(candidate)),
                    "rssi" to Variant((candidate as? BleDiscoveredPebbleDevice)?.rssi ?: 0),
                )
            }
        if (candidates != scanResults) {
            scanResults = candidates
            manager.propertiesChanged(DISCOVERY_INTERFACE, setOf("ScanResults"))
        }
    }

    private fun notificationFilterPropertiesChanged() {
        synchronized(objectsLock) { watches.values.map { it.watch } }
            .forEach { it.propertiesChanged(NOTIFICATIONS_INTERFACE, setOf("Filters")) }
    }

    private fun applicationPropertiesChanged() {
        synchronized(objectsLock) { watches.values.map { it.watch } }
            .forEach { it.propertiesChanged(APPLICATIONS_INTERFACE, setOf("Applications")) }
    }

    private fun screenshotPropertiesChanged() {
        synchronized(objectsLock) { watches.values.map { it.watch } }
            .forEach { it.propertiesChanged(SCREENSHOTS_INTERFACE, setOf("Screenshots")) }
    }

    private fun healthSettingsPropertiesChanged() {
        synchronized(objectsLock) { watches.values.map { it.watch } }
            .forEach { it.propertiesChanged(HEALTH_INTERFACE, setOf("Settings")) }
    }

    private fun timelinePropertiesChanged() {
        synchronized(objectsLock) { watches.values.map { it.watch } }
            .forEach { it.propertiesChanged(TIMELINE_INTERFACE, setOf("CalendarEnabled")) }
    }

    private fun messagingPropertiesChanged() {
        synchronized(objectsLock) { watches.values.map { it.watch } }
            .forEach { it.propertiesChanged(MESSAGING_INTERFACE, setOf("CannedResponses")) }
    }

    private fun libPebbleConfigChanged(change: LibPebbleConfigUpdate) {
        if (change.previous.watchConfig.calendarPins != change.current.watchConfig.calendarPins) {
            timelinePropertiesChanged()
        }
    }

    private inner class RootObject : ObjectManager {
        override fun getObjectPath(): String = ROOT_PATH
        override fun isRemote(): Boolean = false

        override fun GetManagedObjects(): Map<DBusPath, Map<String, Map<String, Variant<*>>>> {
            val result = linkedMapOf<DBusPath, Map<String, Map<String, Variant<*>>>>()
            result[DBusPath(ROOT_PATH)] = mapOf(OBJECT_MANAGER_INTERFACE to emptyMap())
            result[DBusPath(MANAGER_PATH)] = manager.managedInterfaces()
            result[DBusPath(PLATFORM_PATH)] = platform.managedInterfaces()
            synchronized(objectsLock) {
                watches.values.forEach { result[DBusPath(it.path)] = it.watch.managedInterfaces() }
                operations.values.forEach { result[DBusPath(it.path)] = it.managedInterfaces() }
            }
            return result
        }
    }

    /** Shared implementation of org.freedesktop.DBus.Properties for exported objects. */
    private abstract inner class RockpoolObject(
        private val path: String,
    ) : Properties {
        override fun getObjectPath(): String = path
        override fun isRemote(): Boolean = false

        abstract fun managedInterfaces(): Map<String, Map<String, Variant<*>>>

        override fun <A> Get(interfaceName: String, propertyName: String): A {
            val property = managedInterfaces()[interfaceName]?.get(propertyName)
                ?: throw invalidProperty(interfaceName, propertyName)
            @Suppress("UNCHECKED_CAST")
            return property as A
        }

        override fun <A> Set(interfaceName: String, propertyName: String, value: A) {
            throw immutableProperty(interfaceName, propertyName)
        }

        override fun GetAll(interfaceName: String): Map<String, Variant<*>> =
            managedInterfaces()[interfaceName] ?: throw invalidInterface(interfaceName)

        fun propertiesChanged(interfaceName: String, names: Set<String>) {
            val values = managedInterfaces()[interfaceName]
                ?.filterKeys(names::contains)
                .orEmpty()
            if (values.isNotEmpty()) emitProperties(path, interfaceName, values)
        }
    }

    private inner class ManagerObject : RockpoolObject(MANAGER_PATH), RockpoolManager1,
        RockpoolDiscovery1, RockpoolAccount1 {
        override fun managedInterfaces(): Map<String, Map<String, Variant<*>>> = mapOf(
            MANAGER_INTERFACE to mapOf(
                "ApiVersion" to Variant(API_VERSION),
                "DaemonVersion" to Variant(DAEMON_VERSION),
                "State" to Variant(if (connection == null) "starting" else "ready"),
                "Watches" to Variant(watchPaths(), "ao"),
                "Capabilities" to Variant(managerCapabilities(), "as"),
                "LastError" to Variant(""),
            ),
            DISCOVERY_INTERFACE to mapOf(
                "Scanning" to Variant(scanning),
                "ScanResults" to Variant(scanResults, "aa{sv}"),
            ),
            ACCOUNT_INTERFACE to mapOf(
                "Authenticated" to Variant(settings.get("account.oauthToken").isNotEmpty()),
                "Name" to Variant(settings.get("account.name")),
                "Email" to Variant(settings.get("account.email")),
                "SyncState" to Variant(accountSyncCoordinator.state()),
            ),
        )

        override fun Refresh(): DBusPath = operation("manager.refresh") {
            withContext(Dispatchers.IO) { reloadScreenshots() }
            succeed(mapOf("state" to Variant("ready")))
        }

        override fun StartScan(options: Map<String, Variant<*>>): DBusPath = operation("discovery.scan") {
            val transport = primaryScanTransport(options.string("transport"), classicAvailable)
            if (transport == null) {
                fail(ERROR_NOT_SUPPORTED, "requested transport is unavailable")
                return@operation
            }
            // Refreshing this set makes already bonded/dual-mode devices invisible as candidates.
            bondedAddresses = runCatching { BluezManager.bondedAddresses() }.getOrDefault(emptySet())
            val committed = runCommittedScanMutation(::beginCommit) {
                when (transport) {
                    PrimaryDiscoveryTransport.BLE -> libPebble.startBleScan()
                    PrimaryDiscoveryTransport.CLASSIC -> libPebble.startClassicScan()
                }
            }
            if (!committed) return@operation
            succeed(mapOf("transport" to Variant(transport.value)))
        }

        override fun StopScan(): DBusPath = operation("discovery.stop-scan") {
            if (!runCommittedScanMutation(::beginCommit, ::stopAllScans)) return@operation
            succeed()
        }

        override fun Pair(candidate: Map<String, Variant<*>>): DBusPath {
            val address = candidate.string("address")
            val requestedTransport = candidate.string("transport") ?: "any"
            val transportSupported = primaryPairTransportSupported(
                requestedTransport,
                classicAvailable,
            )
            val discovered = if (!address.isNullOrBlank() && transportSupported) {
                libPebble.watches.value.filterIsInstance<DiscoveredPebbleDevice>()
                    .firstOrNull {
                        it.identifier.asString.equals(address, ignoreCase = true) &&
                            primaryPairTransportMatches(
                                requested = requestedTransport,
                                actual = transportOf(it),
                                classicAvailable = classicAvailable,
                            )
                    }
            } else {
                null
            }
            var admittedAttempt: PrimaryConnectionAttemptRegistry.Attempt? = null
            return operation(
                kind = "discovery.pair",
                onPrepared = { pairOperation ->
                    if (!address.isNullOrBlank() && discovered != null) {
                        admittedAttempt = primaryConnectionAttempts.begin(
                            address = address,
                            kind = PrimaryConnectionAttemptKind.PAIR,
                            identifier = discovered.identifier,
                            cancel = pairOperation::cancelFromManager,
                        )
                    }
                },
                onCompletion = {
                    admittedAttempt?.let(primaryConnectionAttempts::abandonPending)
                },
            ) {
                if (address.isNullOrBlank()) {
                    fail(ERROR_INVALID_ARGUMENT, "candidate address is required")
                    return@operation
                }
                if (!transportSupported) {
                    fail(ERROR_NOT_SUPPORTED, "requested transport is unavailable")
                    return@operation
                }
                val device = discovered
                if (device == null) {
                    fail(ERROR_NOT_FOUND, "candidate is no longer visible")
                    return@operation
                }
                val attempt = admittedAttempt
                if (attempt == null) {
                    fail(ERROR_BUSY, "pairing is already active for this candidate")
                    return@operation
                }
                if (!bondedWatchForget.connect(address)) {
                    fail(ERROR_BUSY, "watch forget is committing")
                    return@operation
                }
                var completed = false
                try {
                    currentCoroutineContext().ensureActive()
                    if (!runCommittedScanMutation(::beginCommit, ::stopAllScans)) return@operation
                    if (
                        !primaryConnectionAttempts.requestConnectIfPending(attempt) {
                            requestConnectionImmediately(device.identifier)
                        }
                    ) {
                        fail(ERROR_CANCELLED, "pairing was cancelled")
                        return@operation
                    }
                    setProgress(0.25)
                    when (awaitConnected(address, device)) {
                        ConnectionCompletion.CONNECTED -> {
                            if (primaryConnectionAttempts.complete(attempt)) {
                                completed = true
                                succeed(
                                    mapOf(
                                        "address" to Variant(address),
                                        "transport" to Variant(transportOf(device)),
                                    ),
                                )
                            } else {
                                fail(ERROR_CANCELLED, "pairing was cancelled")
                            }
                        }
                        ConnectionCompletion.FAILED -> fail(ERROR_PAIRING_FAILED, "pairing failed")
                        ConnectionCompletion.TIMED_OUT ->
                            fail(ERROR_PAIRING_FAILED, "pairing timed out")
                    }
                } finally {
                    if (
                        !completed &&
                        !primaryConnectionAttempts.abandonPending(attempt) &&
                        primaryConnectionAttempts.claimCleanup(attempt)
                    ) {
                        cleanupPrimaryConnectionAttempts(listOf(attempt))
                    }
                }
            }
        }

        override fun CancelPairing(): DBusPath = operation("discovery.cancel-pairing") {
            if (!runCommittedScanMutation(::beginCommit, ::stopAllScans)) return@operation
            val attempts = primaryConnectionAttempts.claimAllCleanup(
                PrimaryConnectionAttemptKind.PAIR,
            )
            attempts.forEach { attempt ->
                runCatching(attempt::cancelOperation)
                    .onFailure { logger.w(it) { "could not cancel pairing operation" } }
            }
            cleanupPrimaryConnectionAttempts(attempts)
            succeed()
        }

        override fun ImportBondedWatches(): DBusPath = operation("discovery.import-bonds") {
            when (val result = runBondedWatchImport(importBondedWatches, ::beginCommit)) {
                is BondedWatchImportOperationResult.Completed -> succeed(
                    mapOf(
                        "imported" to Variant(UInt32(result.imported.toLong())),
                        "existing" to Variant(UInt32(result.existing.toLong())),
                        "aliases" to Variant(UInt32(result.aliases.toLong())),
                        "unsupported" to Variant(UInt32(result.unsupported.toLong())),
                    ),
                )
                is BondedWatchImportOperationResult.Failed -> fail(result.error, result.detail)
            }
        }

        override fun SetOAuthToken(token: String): DBusPath {
            val mutation = accountSettings.enqueueTokenMutation(token)
            return try {
                operation(
                    kind = "account.set-token",
                    onCompletion = mutation::release,
                ) {
                    when (mutation.execute(::beginCommit, accountSettings::setToken)) {
                        AccountTokenMutationResult.Saved -> succeed()
                        AccountTokenMutationResult.Cancelled ->
                            fail(ERROR_CANCELLED, "cancelled")
                        AccountTokenMutationResult.PersistenceFailed ->
                            fail(ERROR_IO, "could not save account credentials")
                    }
                }
            } catch (e: Exception) {
                mutation.release()
                throw e
            }
        }

        override fun Sync(): DBusPath = operation("account.sync") {
            if (settings.get("account.oauthToken").isEmpty()) {
                fail(ERROR_AUTHENTICATION_FAILED, "account sign-in is required")
                return@operation
            }
            when (
                val result = accountSyncCoordinator.synchronize(
                    beginCommit = ::beginCommit,
                    start = libPebble::requestLockerSync,
                )
            ) {
                AccountSyncResult.Completed -> succeed()
                AccountSyncResult.Busy ->
                    fail(ERROR_BUSY, "account synchronization is already active")
                AccountSyncResult.AccountChanged ->
                    fail(ERROR_AUTHENTICATION_FAILED, "account changed during synchronization")
                AccountSyncResult.Cancelled -> fail(ERROR_CANCELLED, "cancelled")
                is AccountSyncResult.Failed -> {
                    logger.w(result.cause) { "account locker synchronization failed" }
                    fail(ERROR_TRANSPORT_FAILED, "account synchronization failed")
                }
            }
        }
    }

    private inner class PlatformObject : RockpoolObject(PLATFORM_PATH), RockpoolPlatform1 {
        override fun managedInterfaces(): Map<String, Map<String, Variant<*>>> {
            val snapshot = platformProvider.snapshot()
            return mapOf(
                PLATFORM_INTERFACE to mapOf(
                    "Provider" to Variant(snapshot.provider),
                    "AbiVersion" to Variant(snapshot.abiVersion),
                    "BuildId" to Variant(snapshot.buildId),
                    "HelperPid" to Variant(UInt32(snapshot.helperPid.coerceIn(0, UINT32_MAX))),
                    "Health" to Variant(platformHealth(snapshot), "a{sv}"),
                    "Capabilities" to Variant(platformCapabilities(snapshot), "as"),
                    "LastError" to Variant(
                        if (snapshot.state == "ready") "" else ERROR_PROVIDER_UNAVAILABLE,
                    ),
                ),
            )
        }

        override fun Restart(): DBusPath = operation("platform.restart") {
            setProgress(0.1)
            val snapshot = platformProvider.restart(::beginCommit) ?: return@operation
            propertiesChanged(PLATFORM_INTERFACE, managedInterfaces()[PLATFORM_INTERFACE]?.keys.orEmpty())
            if (snapshot.isRestartOperational()) {
                succeed(mapOf("provider" to Variant(snapshot.provider)))
            } else {
                fail(ERROR_PROVIDER_UNAVAILABLE, "platform provider is unavailable")
            }
        }
    }

    private enum class ConnectionCompletion {
        CONNECTED,
        FAILED,
        TIMED_OUT,
    }

    /** Wait for the libpebble state transition; connect() itself only starts it. */
    private suspend fun awaitConnected(
        address: String,
        initial: PebbleDevice,
    ): ConnectionCompletion {
        val terminal = withTimeoutOrNull(CONNECTION_TIMEOUT) {
            libPebble.watches
                .map { devices -> devices.firstOrNull { it.matchesAddress(address) } }
                .filterNotNull()
                .first { candidate ->
                    connectionAttemptIsTerminal(
                        isConnected = candidate is CommonConnectedDevice,
                        initialFailure = initial.connectionFailureInfo,
                        currentFailure = candidate.connectionFailureInfo,
                    )
                }
        }
        return when {
            terminal == null -> ConnectionCompletion.TIMED_OUT
            terminal is CommonConnectedDevice -> ConnectionCompletion.CONNECTED
            else -> ConnectionCompletion.FAILED
        }
    }

    private suspend fun awaitDisconnected(address: String): Boolean =
        withTimeoutOrNull(DISCONNECT_TIMEOUT) {
            libPebble.watches.first { devices ->
                devices.none { it.matchesAddress(address) && it is ActiveDevice }
            }
            true
        } == true

    /**
     * Retire an ordered libpebble3 goal and retain address ownership until WatchManager confirms
     * that neither the goal nor its transport attempt remains. The timeout bounds only the public
     * operation; a service-scope reaper keeps ownership afterwards so a late connection can never
     * overlap a replacement attempt.
     */
    private suspend fun cleanupPrimaryConnectionAttempts(
        attempts: Collection<PrimaryConnectionAttemptRegistry.Attempt>,
    ) {
        if (attempts.isEmpty()) return
        withContext(NonCancellable) {
            attempts.forEach { attempt ->
                val identifier = attempt.identifier
                if (identifier == null) {
                    logger.e { "connection attempt has no identifier for ${attempt.address}" }
                    return@forEach
                }

                runCatching {
                    primaryConnectionAttempts.retireConnectionIfCleaning(attempt) {
                        cancelConnectionImmediately(identifier)
                    }
                }.onFailure {
                    logger.w(it) { "could not retire connection goal for ${attempt.address}" }
                }

                if (!primaryConnectionAttempts.claimRetirementWait(attempt)) return@forEach
                val retired = runCatching {
                    withTimeoutOrNull(PAIR_CLEANUP_TIMEOUT) {
                        awaitConnectionRetired(identifier)
                        true
                    } == true
                }.onFailure {
                    logger.w(it) { "could not observe retirement for ${attempt.address}" }
                }.getOrDefault(false)

                if (retired) {
                    primaryConnectionAttempts.finishCleanup(attempt)
                } else {
                    logger.w {
                        "timed out retiring connection for ${attempt.address}; retaining ownership"
                    }
                    scope.launch {
                        try {
                            awaitConnectionRetired(identifier)
                            primaryConnectionAttempts.finishCleanup(attempt)
                        } catch (e: CancellationException) {
                            throw e
                        } catch (e: Exception) {
                            logger.e("connection retirement reaper failed", e)
                        }
                    }
                }
            }
        }
    }

    private suspend fun awaitForgotten(address: String): Boolean =
        withTimeoutOrNull(DISCONNECT_TIMEOUT) {
            libPebble.watches.first { devices ->
                devices.none { it.matchesAddress(address) && it is KnownPebbleDevice }
            }
            true
        } == true

    private fun PebbleDevice.matchesAddress(address: String): Boolean =
        identifier.asString.equals(address, ignoreCase = true)

    private inner class RockpoolWatchObject(
        private val objectId: String,
        path: String,
        address: String,
    ) : RockpoolObject(path),
        RockpoolWatch1,
        RockpoolFirmware1,
        RockpoolApplications1,
        RockpoolTimeline1,
        RockpoolNotifications1,
        RockpoolMessaging1,
        RockpoolHealth1,
        RockpoolProfiles1,
        RockpoolScreenshots1,
        RockpoolLogs1,
        RockpoolDeveloper1 {
        @Volatile
        private var address = address
        private val screenshotCaptureActive = AtomicBoolean(false)

        fun updateAddress(value: String) {
            address = value
        }

        fun matchesAddress(value: String): Boolean = address.equals(value, ignoreCase = true)

        private fun device(): PebbleDevice? = libPebble.watches.value.firstOrNull {
            it.identifier.asString.equals(address, ignoreCase = true)
        }

        private val settingPrefix: String
            get() = "watch.$objectId"

        private fun importedNotificationFilters(): List<Map<String, Variant<*>>> {
            val globalValues = settings.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
            val prefix = if (hasCanonicalNotificationFilters(globalValues)) {
                GLOBAL_NOTIFICATION_FILTER_PREFIX
            } else {
                "$settingPrefix.notifications."
            }
            val values = if (prefix == GLOBAL_NOTIFICATION_FILTER_PREFIX) {
                globalValues
            } else {
                settings.entries(prefix)
            }
            return values.entries
                .filter { it.key.endsWith(".source") }
                .sortedBy { it.key }
                .map { (sourceKey, source) ->
                    val id = sourceKey.removePrefix(prefix).removeSuffix(".source")
                    record(
                        "id" to Variant(id),
                        "source" to Variant(source),
                        "enabled" to Variant(
                            storedNotificationEnabled(values["$prefix$id.enabled"])
                        ),
                        "name" to Variant(values["$prefix$id.name"].orEmpty()),
                        "icon" to Variant(values["$prefix$id.icon"].orEmpty()),
                    )
                }
        }

        private fun primaryCannedResponses(): List<Map<String, Variant<*>>> {
            return primaryCannedResponseRecords(
                settings = settings.entries(PRIMARY_CANNED_PREFIX),
                prefix = PRIMARY_CANNED_PREFIX,
            )
        }

        override fun managedInterfaces(): Map<String, Map<String, Variant<*>>> {
            val device = device()
            val known = device as? KnownPebbleDevice
            val connected = device as? CommonConnectedDevice
            return mapOf(
                WATCH_INTERFACE to mapOf(
                    "Id" to Variant(objectId),
                    "Name" to Variant(device?.displayName().orEmpty()),
                    "Serial" to Variant(known?.serial.orEmpty()),
                    "Transport" to Variant(transportOf(device)),
                    "ConnectionState" to Variant(connectionStateOf(device)),
                    "LastError" to Variant(sanitizedConnectionError(device)),
                    "Capabilities" to Variant(primaryWatchCapabilities(), "as"),
                ),
                FIRMWARE_INTERFACE to mapOf(
                    "FirmwareVersion" to Variant(known?.runningFwVersion.orEmpty()),
                    "LanguageVersion" to Variant(connected?.watchInfo?.language.orEmpty()),
                ),
                APPLICATIONS_INTERFACE to mapOf(
                    "Applications" to Variant(
                        known?.let {
                            rockpoolApplicationRecords(lockerApplications, it.watchType.watchType)
                        }
                            .orEmpty(),
                        "aa{sv}",
                    ),
                ),
                TIMELINE_INTERFACE to mapOf(
                    "CalendarEnabled" to Variant(libPebble.config.value.watchConfig.calendarPins),
                ),
                NOTIFICATIONS_INTERFACE to mapOf(
                    "Filters" to Variant(importedNotificationFilters(), "aa{sv}"),
                ),
                MESSAGING_INTERFACE to mapOf(
                    "CannedResponses" to Variant(primaryCannedResponses(), "aa{sv}"),
                ),
                HEALTH_INTERFACE to mapOf(
                    "Settings" to Variant(
                        primaryHealthSettings(healthSettings.current()),
                        "a{sv}",
                    ),
                ),
                PROFILES_INTERFACE to mapOf(
                    "Profiles" to Variant(
                        mapOf(
                            "connected" to Variant(
                                settings.get("$settingPrefix.profiles.connected"),
                            ),
                            "disconnected" to Variant(
                                settings.get("$settingPrefix.profiles.disconnected"),
                            ),
                        ),
                        "a{sv}",
                    ),
                ),
                SCREENSHOTS_INTERFACE to mapOf(
                    "Screenshots" to Variant(
                        screenshots.map(RockpoolScreenshotRecord::asVariantMap),
                        "aa{sv}",
                    ),
                ),
                LOGS_INTERFACE to mapOf(
                    "DumpPath" to Variant("~/Downloads/pebble.log"),
                ),
                DEVELOPER_INTERFACE to mapOf(
                    "LocalEnabled" to Variant(
                        (device() as? CommonConnectedDevice)?.devConnectionActive?.value ?: false,
                    ),
                ),
            )
        }

        fun propertiesChangedAll() {
            val interfaces = managedInterfaces()
            propertiesChanged(WATCH_INTERFACE, interfaces[WATCH_INTERFACE]?.keys.orEmpty())
            propertiesChanged(FIRMWARE_INTERFACE, interfaces[FIRMWARE_INTERFACE]?.keys.orEmpty())
            // Application compatibility depends on the watch model. WatchManager can replace
            // the model metadata for a stable serial/address without replacing this D-Bus path.
            propertiesChanged(APPLICATIONS_INTERFACE, setOf("Applications"))
        }

        override fun Connect(): DBusPath {
            val initialDevice = device()
            var admittedAttempt: PrimaryConnectionAttemptRegistry.Attempt? = null
            return operation(
                kind = "watch.connect",
                onPrepared = { connectOperation ->
                    if (initialDevice != null) {
                        admittedAttempt = primaryConnectionAttempts.begin(
                            address = address,
                            kind = PrimaryConnectionAttemptKind.WATCH_CONNECT,
                            identifier = initialDevice.identifier,
                            cancel = connectOperation::cancelFromManager,
                        )
                    }
                },
                onCompletion = {
                    admittedAttempt?.let(primaryConnectionAttempts::abandonPending)
                },
            ) {
                val device = device()
                if (device == null) {
                    fail(ERROR_NOT_FOUND, "watch is no longer known")
                    return@operation
                }
                val attempt = admittedAttempt
                if (attempt == null) {
                    fail(ERROR_BUSY, "a connection attempt is already active for this watch")
                    return@operation
                }
                if (!bondedWatchForget.connect(address)) {
                    fail(ERROR_BUSY, "watch forget is committing")
                    return@operation
                }
                var completed = false
                try {
                    currentCoroutineContext().ensureActive()
                    if (
                        !primaryConnectionAttempts.requestConnectIfPending(attempt) {
                            requestConnectionImmediately(device.identifier)
                        }
                    ) {
                        fail(ERROR_CANCELLED, "connection was cancelled")
                        return@operation
                    }
                    setProgress(0.25)
                    when (awaitConnected(address, device)) {
                        ConnectionCompletion.CONNECTED -> {
                            if (beginCommit() && primaryConnectionAttempts.complete(attempt)) {
                                completed = true
                                succeed()
                            } else {
                                fail(ERROR_CANCELLED, "connection was cancelled")
                            }
                        }
                        ConnectionCompletion.FAILED ->
                            fail(ERROR_TRANSPORT_FAILED, "watch connection failed")
                        ConnectionCompletion.TIMED_OUT ->
                            fail(ERROR_TRANSPORT_FAILED, "watch connection timed out")
                    }
                } finally {
                    if (
                        !completed &&
                        !primaryConnectionAttempts.abandonPending(attempt) &&
                        primaryConnectionAttempts.claimCleanup(attempt)
                    ) {
                        cleanupPrimaryConnectionAttempts(listOf(attempt))
                    }
                }
            }
        }

        override fun Disconnect(): DBusPath = operation("watch.disconnect") {
            val active = device() as? ActiveDevice
            if (active == null) {
                fail(ERROR_NOT_CONNECTED, "watch is not connected")
                return@operation
            }
            if (!beginCommit()) return@operation
            active.disconnect()
            setProgress(0.25)
            if (awaitDisconnected(address)) {
                succeed()
            } else {
                fail(ERROR_TRANSPORT_FAILED, "watch disconnect timed out")
            }
        }

        override fun Forget(): DBusPath = operation("watch.forget") {
            val known = device() as? KnownPebbleDevice
            if (known == null) {
                fail(ERROR_NOT_FOUND, "watch is no longer known")
                return@operation
            }
            when (
                bondedWatchForget.forget(
                    address = address,
                    name = known.name,
                    beginCommit = ::beginCommit,
                    disconnect = { (known as? ActiveDevice)?.disconnect() },
                    forgetPortable = known::forget,
                )
            ) {
                BondedWatchForgetOutcome.COMPLETED -> {
                    if (awaitForgotten(address)) {
                        succeed()
                    } else {
                        fail(ERROR_TRANSPORT_FAILED, "watch removal timed out")
                    }
                }
                BondedWatchForgetOutcome.UNAVAILABLE ->
                    fail(ERROR_TRANSPORT_FAILED, "BlueZ bond state is unavailable")
                BondedWatchForgetOutcome.BUSY ->
                    fail(ERROR_BUSY, "watch forget is already committing")
                BondedWatchForgetOutcome.SUPERSEDED ->
                    fail(ERROR_CANCELLED, "watch forget was superseded by a connection")
                BondedWatchForgetOutcome.CANCELLED ->
                    fail(ERROR_CANCELLED, "watch forget was cancelled")
                BondedWatchForgetOutcome.REMOVAL_FAILED ->
                    fail(ERROR_TRANSPORT_FAILED, "could not remove every BlueZ bond")
            }
        }

        override fun InstallFirmware(firmware: FileDescriptor): DBusPath =
            unavailable("watch.install-firmware", "firmware installation is not available yet")

        override fun InstallLanguagePack(languagePack: FileDescriptor): DBusPath =
            unavailable("watch.install-language", "language-pack installation is not available yet")

        override fun Install(pbw: FileDescriptor): DBusPath =
            unavailable("watch.install-app", "application installation is not available yet")

        override fun Remove(uuid: String): DBusPath = operation("watch.remove-app") {
            val applicationId = parseCanonicalApplicationUuid(uuid)
            if (applicationId == null) {
                fail(ERROR_INVALID_ARGUMENT, "application UUID is invalid")
                return@operation
            }
            val application = libPebble.getLockerApp(applicationId).first()
            if (application == null) {
                fail(ERROR_NOT_FOUND, "application is not in the locker")
                return@operation
            }
            val known = device() as? KnownPebbleDevice
            if (known == null) {
                fail(ERROR_NOT_FOUND, "watch is no longer known")
                return@operation
            }
            when (rockpoolApplicationRemovalEligibility(application, known.watchType.watchType)) {
                RockpoolApplicationRemovalEligibility.NOT_INSTALLED -> {
                    fail(ERROR_NOT_FOUND, "application is not installed on this watch")
                    return@operation
                }
                RockpoolApplicationRemovalEligibility.SYSTEM -> {
                    fail(ERROR_NOT_SUPPORTED, "system applications cannot be removed")
                    return@operation
                }
                RockpoolApplicationRemovalEligibility.REMOVABLE -> Unit
            }
            setProgress(0.25)
            if (!beginCommit()) return@operation
            val removed = withContext(NonCancellable) {
                libPebble.removeApp(applicationId)
            }
            if (!removed) {
                fail(ERROR_TRANSPORT_FAILED, "application could not be removed")
                return@operation
            }
            succeed(mapOf("uuid" to Variant(applicationId.toString())))
        }

        override fun Sync(): DBusPath =
            unavailable("watch.timeline-sync", "timeline sync is not available yet")

        override fun SetFilters(filters: List<Map<String, Variant<*>>>): DBusPath =
            operation("watch.set-notification-filters") {
                val values = normalizeNotificationFilters(filters) ?: return@operation
                val prefix = "$settingPrefix.notifications."
                val configured = notificationFiltersFromEntries(values)
                if (!beginCommit()) return@operation
                // Persistence is the commit point.  Finish the bounded runtime
                // handoff even if the caller races Cancel, so Operation1 cannot
                // report "cancelled" after committing a different filter set.
                val result = withContext(NonCancellable) {
                    notificationFilters.replacePrimary(prefix, values, configured)
                }
                if (result == null) {
                    fail(ERROR_IO, "could not save notification filters")
                    return@operation
                }
                if (!result.runtimePolicyUpdated) {
                    fail(
                        ERROR_IO,
                        "notification filters were saved but provider policy is pending",
                    )
                    return@operation
                }
                if (!result.applicationStateUpdated) {
                    fail(
                        ERROR_IO,
                        "notification filters were saved but application state is pending",
                    )
                    return@operation
                }
                succeed(
                    mapOf(
                        "count" to Variant(filters.size),
                        "activePinsUpdated" to Variant(result.activePinsUpdated),
                    )
                )
            }

        override fun SetCannedResponses(responses: List<Map<String, Variant<*>>>): DBusPath =
            operation("watch.set-canned-responses") {
                val values = normalizeCannedResponses(responses) ?: return@operation
                val cannedResponses = flattenPrimaryCannedResponses(
                    responses.map { response ->
                        PrimaryCannedResponseGroup(
                            name = checkNotNull(response.stringValue("name")),
                            values = checkNotNull(response.stringListValue("values")),
                        )
                    },
                )
                when (cannedResponsesReconciler.replace(values, cannedResponses, ::beginCommit)) {
                    PrimaryCannedResponsesUpdateResult.Completed -> Unit
                    PrimaryCannedResponsesUpdateResult.Cancelled -> return@operation
                    PrimaryCannedResponsesUpdateResult.InvalidArgument -> {
                        fail(ERROR_INVALID_ARGUMENT, "canned responses exceed storage capacity")
                        return@operation
                    }
                    PrimaryCannedResponsesUpdateResult.PersistenceFailed -> {
                        fail(ERROR_IO, "could not save canned responses")
                        return@operation
                    }
                    PrimaryCannedResponsesUpdateResult.ActivationFailed -> {
                        fail(ERROR_IO, "could not activate canned responses")
                        return@operation
                    }
                }
                messagingPropertiesChanged()
                succeed(
                    mapOf(
                        "count" to Variant(responses.size),
                        "responses" to Variant(cannedResponses.size),
                    ),
                )
            }

        override fun SetSettings(settings: Map<String, Variant<*>>): DBusPath =
            operation("watch.set-health") {
                val normalized = normalizeHealthSettings(settings) ?: return@operation
                val update = try {
                    healthSettings.updateWithResultSuspend(
                        transform = { current -> mergePrimaryHealthSettings(current, normalized) },
                        commit = ::beginCommit,
                    )
                } catch (e: IllegalArgumentException) {
                    fail(ERROR_INVALID_ARGUMENT, "health settings cannot be represented by the watch")
                    return@operation
                } catch (e: Exception) {
                    logger.w(e) { "health settings update failed" }
                    fail(ERROR_IO, "health settings could not be saved")
                    return@operation
                }
                if (update.saved) succeed()
            }

        override fun SetProfiles(profiles: Map<String, Variant<*>>): DBusPath =
            operation("watch.set-profiles") {
                val normalized = normalizeProfiles(profiles) ?: return@operation
                if (!beginCommit()) return@operation
                if (!profileSettings.updateByObjectId(objectId, address, normalized)) {
                    fail(ERROR_IO, "could not save profiles")
                    return@operation
                }
                succeed()
            }

        override fun Capture(): DBusPath = operation("watch.capture-screenshot") {
            if (!screenshotCaptureActive.compareAndSet(false, true)) {
                fail(ERROR_BUSY, "screenshot capture is already active")
                return@operation
            }
            try {
                val connected = device() as? ConnectedPebbleDevice
                if (connected == null) {
                    fail(ERROR_NOT_CONNECTED, "screenshot capture needs a connected compatible watch")
                    return@operation
                }
                setProgress(0.1)
                val bitmap = connected.takeScreenshot()
                if (bitmap == null) {
                    fail(ERROR_TRANSPORT_FAILED, "watch did not provide a screenshot")
                    return@operation
                }
                currentCoroutineContext().ensureActive()

                val png = try {
                    val pixels = IntArray(rockpoolScreenshotPixelCount(bitmap.width, bitmap.height))
                    bitmap.readPixels(pixels)
                    currentCoroutineContext().ensureActive()
                    setProgress(0.65)
                    encodeRockpoolPngRgba(bitmap.width, bitmap.height, pixels)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "watch returned an invalid screenshot" }
                    fail(ERROR_TRANSPORT_FAILED, "watch returned an invalid screenshot")
                    return@operation
                }

                val record = try {
                    withContext(Dispatchers.IO) {
                        screenshotStoreMutex.withLock {
                            val stored = screenshotStore.write(png, ::beginCommit)
                            screenshots = (screenshots + stored).sortedWith(
                                compareByDescending<RockpoolScreenshotRecord> { it.createdMillis }
                                    .thenByDescending { it.id },
                            )
                            stored
                        }
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.e("could not save screenshot", e)
                    fail(ERROR_IO, "screenshot could not be saved")
                    return@operation
                }
                screenshotPropertiesChanged()
                succeed(record.asVariantMap())
            } finally {
                screenshotCaptureActive.set(false)
            }
        }

        override fun Dump(): DBusPath = operation("watch.dump-logs") {
            val connected = device() as? CommonConnectedDevice
            if (connected == null) {
                fail(ERROR_NOT_CONNECTED, "watch is not connected")
                return@operation
            }
            val source = connected.gatherLogs()
            if (source == null) {
                fail(ERROR_IO, "watch did not provide logs")
                return@operation
            }
            setProgress(0.75)
            if (!beginCommit()) return@operation
            val target = writeFixedLogFile(File(source.toString()))
            succeed(mapOf("path" to Variant(target.toString())))
        }

        override fun SetLocalEnabled(enabled: Boolean): DBusPath =
            operation("watch.set-local-developer-mode") {
                val connected = device() as? CommonConnectedDevice
                if (connected == null) {
                    fail(ERROR_NOT_CONNECTED, "developer mode needs a connected compatible watch")
                    return@operation
                }
                try {
                    if (!beginCommit()) return@operation
                    if (enabled) connected.startDevConnection() else connected.stopDevConnection()
                    if (connected.devConnectionActive.value != enabled) {
                        fail(
                            if (enabled) ERROR_BUSY else ERROR_TRANSPORT_FAILED,
                            if (enabled) {
                                "another watch already owns the local developer connection"
                            } else {
                                "developer mode did not stop"
                            },
                        )
                        return@operation
                    }
                    propertiesChanged(DEVELOPER_INTERFACE, setOf("LocalEnabled"))
                    succeed(mapOf("enabled" to Variant(enabled)))
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w { "developer mode change failed: ${e.message}" }
                    fail(ERROR_TRANSPORT_FAILED, "developer mode change failed")
                }
            }

        private fun RockpoolOperationObject.normalizeNotificationFilters(
            filters: List<Map<String, Variant<*>>>,
        ): Map<String, String>? {
            if (filters.size > MAX_NOTIFICATION_FILTERS) {
                fail(ERROR_INVALID_ARGUMENT, "too many notification filters")
                return null
            }
            val values = linkedMapOf<String, String>()
            val sources = HashSet<String>()
            filters.forEach { filter ->
                if (filter.keys.any { it !in NOTIFICATION_FILTER_FIELDS }) {
                    fail(ERROR_INVALID_ARGUMENT, "unknown notification filter field")
                    return null
                }
                val source = filter.stringValue("source")
                if (source.isNullOrBlank() || source.length > MAX_SETTING_STRING || !sources.add(source)) {
                    fail(ERROR_INVALID_ARGUMENT, "each notification filter needs a unique source")
                    return null
                }
                val enabled = filter.booleanValue("enabled")
                if (enabled == null) {
                    fail(ERROR_INVALID_ARGUMENT, "notification filter enabled must be boolean")
                    return null
                }
                val name = filter.optionalStringValue("name")
                val icon = filter.optionalStringValue("icon")
                val id = settingId(source)
                if (("name" in filter && name == null) || ("icon" in filter && icon == null) ||
                    !filter.matchesOptionalId(id)
                ) {
                    fail(ERROR_INVALID_ARGUMENT, "invalid notification filter field")
                    return null
                }
                val effectiveName = name ?: source
                val effectiveIcon = icon ?: ""
                if (effectiveName.length > MAX_SETTING_STRING || effectiveIcon.length > MAX_SETTING_STRING) {
                    fail(ERROR_INVALID_ARGUMENT, "notification filter text is too long")
                    return null
                }
                val prefix = "$settingPrefix.notifications.$id."
                values["${prefix}source"] = source
                values["${prefix}enabled"] = if (enabled) {
                    NOTIFICATION_ENABLED.toString()
                } else {
                    NOTIFICATION_DISABLED.toString()
                }
                values["${prefix}name"] = effectiveName
                values["${prefix}icon"] = effectiveIcon
            }
            return values
        }

        private fun RockpoolOperationObject.normalizeCannedResponses(
            responses: List<Map<String, Variant<*>>>,
        ): Map<String, String>? {
            if (responses.size > MAX_CANNED_GROUPS) {
                fail(ERROR_INVALID_ARGUMENT, "too many canned response groups")
                return null
            }
            val values = linkedMapOf<String, String>()
            val names = HashSet<String>()
            responses.forEach { response ->
                if (response.keys.any { it !in CANNED_RESPONSE_FIELDS }) {
                    fail(ERROR_INVALID_ARGUMENT, "unknown canned response field")
                    return null
                }
                val name = response.stringValue("name")
                val entries = response.stringListValue("values")
                val id = name?.let(::settingId)
                if (name.isNullOrBlank() || name.length > MAX_SETTING_STRING || !names.add(name) ||
                    entries == null || entries.size > MAX_CANNED_RESPONSES ||
                    entries.any { it.length > MAX_SETTING_STRING || SETTINGS_LIST_SEPARATOR in it } ||
                    id == null || !response.matchesOptionalId(id)
                ) {
                    fail(ERROR_INVALID_ARGUMENT, "invalid canned response group")
                    return null
                }
                val prefix = "$PRIMARY_CANNED_PREFIX$id."
                values["${prefix}name"] = name
                values["${prefix}values"] = entries.joinToString(SETTINGS_LIST_SEPARATOR)
            }
            return values
        }

        private fun RockpoolOperationObject.normalizeHealthSettings(
            values: Map<String, Variant<*>>,
        ): Map<String, String>? {
            if (values.isEmpty() || values.keys.any { it !in HEALTH_SETTING_KEYS }) {
                fail(ERROR_INVALID_ARGUMENT, "unknown or empty health settings")
                return null
            }
            val normalized = linkedMapOf<String, String>()
            values.forEach { (key, variant) ->
                val value = when (key) {
                    "enabled", "moreActive", "sleepMore", "imperialUnits" ->
                        variant.booleanValue()?.toString()
                    "age" -> variant.intValue()?.takeIf { it in 1..130 }?.toString()
                    "height" -> variant.intValue()?.takeIf { it in 50..300 }?.toString()
                    "gender" -> variant.intValue()?.takeIf { it in 0..2 }?.toString()
                    "weight" -> variant.intValue()?.takeIf { it in 10..327 }?.toString()
                    else -> null
                }
                if (value == null) {
                    fail(ERROR_INVALID_ARGUMENT, "invalid health setting $key")
                    return null
                }
                val settingKey = if (key == "imperialUnits") "units.imperial" else "health.$key"
                normalized[settingKey] = value
            }
            return normalized
        }

        private fun RockpoolOperationObject.normalizeProfiles(
            profiles: Map<String, Variant<*>>,
        ): Map<String, String>? {
            if (profiles.isEmpty() || profiles.keys.any { it !in PROFILE_KEYS }) {
                fail(ERROR_INVALID_ARGUMENT, "unknown or empty profile settings")
                return null
            }
            val normalized = linkedMapOf<String, String>()
            profiles.forEach { (key, value) ->
                val profile = value.value as? String
                if (profile == null || profile.length > MAX_SETTING_STRING) {
                    fail(ERROR_INVALID_ARGUMENT, "invalid profile $key")
                    return null
                }
                normalized[key] = profile
            }
            return normalized
        }

        private fun Map<String, Variant<*>>.stringValue(name: String): String? =
            this[name]?.value as? String

        private fun Map<String, Variant<*>>.optionalStringValue(name: String): String? =
            when (val value = this[name]?.value) {
                null -> null
                is String -> value
                else -> null
            }

        private fun Map<String, Variant<*>>.booleanValue(name: String): Boolean? =
            this[name]?.booleanValue()

        private fun Variant<*>.booleanValue(): Boolean? = value as? Boolean

        private fun Variant<*>.intValue(): Int? = when (val raw = value) {
            is Byte -> raw.toInt()
            is Short -> raw.toInt()
            is Int -> raw
            is Long -> raw.takeIf { it in Int.MIN_VALUE.toLong()..Int.MAX_VALUE.toLong() }?.toInt()
            is UInt32 -> raw.toLong().takeIf { it <= Int.MAX_VALUE }?.toInt()
            else -> null
        }

        private fun Map<String, Variant<*>>.stringListValue(name: String): List<String>? =
            (this[name]?.value as? List<*>)?.map { it as? String ?: return null }

        private fun Map<String, Variant<*>>.matchesOptionalId(expected: String): Boolean =
            when (val value = this["id"]?.value) {
                null -> true
                is String -> value == expected
                else -> false
            }

        private fun unavailable(kind: String, detail: String): DBusPath = operation(kind) {
            fail(ERROR_NOT_SUPPORTED, detail)
        }
    }

    private inner class RockpoolOperationObject(
        val path: String,
        private val kind: String,
    ) : RockpoolObject(path), RockpoolOperation1 {
        private val stateLock = Any()
        private var job: Job? = null
        private var state = "pending"
        private var progress = 0.0
        private var result: Map<String, Variant<*>> = emptyMap()
        private var error = ""
        private var errorDetail = ""
        private var completionSent = false
        private var acceptsCancellation = true

        override fun managedInterfaces(): Map<String, Map<String, Variant<*>>> = synchronized(stateLock) {
            mapOf(
                OPERATION_INTERFACE to mapOf(
                    "Kind" to Variant(kind),
                    "State" to Variant(state),
                    "Progress" to Variant(progress),
                    "Result" to Variant(result, "a{sv}"),
                    "Error" to Variant(error),
                    "ErrorDetail" to Variant(errorDetail),
                ),
            )
        }

        /**
         * Allocate the coroutine before exporting this object.  A client can
         * call Cancel as soon as InterfacesAdded is delivered, so assigning a
         * lazy job later would lose that cancellation race.
         */
        fun prepare(
            onCompletion: (() -> Unit)?,
            task: suspend RockpoolOperationObject.() -> Unit,
        ) {
            check(job == null) { "operation job already prepared" }
            val prepared = scope.launch(start = CoroutineStart.LAZY) {
                if (terminal()) return@launch
                setState("running")
                try {
                    task()
                    if (!terminal()) succeed()
                } catch (_: CancellationException) {
                    fail(ERROR_CANCELLED, "cancelled")
                } catch (e: Exception) {
                    logger.e("$kind failed", e)
                    fail(ERROR_INTERNAL, "operation failed")
                }
            }
            onCompletion?.let { completion ->
                prepared.invokeOnCompletion {
                    runCatching(completion)
                        .onFailure { logger.e("$kind completion hook failed", it) }
                }
            }
            job = prepared
        }

        fun start() {
            job?.start()
        }

        fun abort() {
            job?.cancel()
        }

        override fun Cancel() {
            val cancellable = synchronized(stateLock) {
                if (isTerminalLocked() || !acceptsCancellation) return
                acceptsCancellation = false
                job
            }
            cancellable?.cancel()
            // Set the terminal result synchronously so cancelling a lazy job
            // cannot leave it indefinitely in a non-terminal state.
            fail(ERROR_CANCELLED, "cancelled")
        }

        /** A committed CancelPairing operation remains authoritative after Pair begins. */
        fun cancelFromManager() {
            val cancellable = synchronized(stateLock) {
                if (isTerminalLocked()) return
                acceptsCancellation = false
                job
            }
            cancellable?.cancel()
            fail(ERROR_CANCELLED, "cancelled")
        }

        /**
         * Cross the irreversible side-effect boundary. Cancel wins if it reaches the lock first;
         * after this returns true, later Cancel calls are ignored and the operation reports the
         * actual committed outcome.
         */
        fun beginCommit(): Boolean = synchronized(stateLock) {
            if (isTerminalLocked() || !acceptsCancellation) return@synchronized false
            acceptsCancellation = false
            true
        }

        fun setProgress(value: Double) {
            synchronized(stateLock) { progress = value.coerceIn(0.0, 1.0) }
            propertiesChanged(OPERATION_INTERFACE, setOf("Progress"))
        }

        fun succeed(values: Map<String, Variant<*>> = emptyMap()) {
            val shouldSignal = synchronized(stateLock) {
                if (isTerminalLocked()) return
                state = "succeeded"
                progress = 1.0
                result = values
                error = ""
                errorDetail = ""
                !completionSent.also { completionSent = true }
            }
            propertiesChanged(
                OPERATION_INTERFACE,
                setOf("State", "Progress", "Result", "Error", "ErrorDetail"),
            )
            if (shouldSignal) emitSignal(RockpoolOperation1.Completed(path, true))
        }

        fun fail(stableError: String, detail: String) {
            val shouldSignal = synchronized(stateLock) {
                if (isTerminalLocked()) return
                state = if (stableError == ERROR_CANCELLED) "cancelled" else "failed"
                error = stableError
                errorDetail = detail
                !completionSent.also { completionSent = true }
            }
            propertiesChanged(
                OPERATION_INTERFACE,
                setOf("State", "Error", "ErrorDetail"),
            )
            if (shouldSignal) emitSignal(RockpoolOperation1.Completed(path, false))
        }

        private fun setState(value: String) {
            synchronized(stateLock) {
                if (isTerminalLocked()) return
                state = value
            }
            propertiesChanged(OPERATION_INTERFACE, setOf("State"))
        }

        fun terminal(): Boolean = synchronized(stateLock) { isTerminalLocked() }

        private fun isTerminalLocked(): Boolean = state in setOf("succeeded", "failed", "cancelled")
    }

    private fun operation(
        kind: String,
        onCompletion: (() -> Unit)? = null,
        onPrepared: ((RockpoolOperationObject) -> Unit)? = null,
        task: suspend RockpoolOperationObject.() -> Unit,
    ): DBusPath {
        val path = "$OPERATIONS_PATH/op_${UUID.randomUUID().toString().replace("-", "")}"
        val operation = RockpoolOperationObject(path, kind)
        operation.prepare(onCompletion, task)
        try {
            onPrepared?.invoke(operation)
            val publication = managedObjects.mutate {
                val old = operations.values.filter { it.terminal() }
                    .take((operations.size - MAX_OPERATIONS + 1).coerceAtLeast(0))
                old.forEach {
                    remove(
                        unexport = { conn -> conn.unExportObject(it.path) },
                        hide = { operations.remove(it.path) },
                    )
                }
                publish(
                    export = { it.exportObject(path, operation) },
                    makeVisible = { operations[path] = operation },
                )
                old
            }
            publication.failure?.let {
                logger.e("could not update primary operation objects; reconnecting", it)
            }
            publication.lostConnection?.let(::finishConnectionLoss)
            publication.value.forEach {
                emitRemoved(publication.connection, it.path, it.managedInterfaces().keys)
            }
            emitAdded(publication.connection, path, operation.managedInterfaces())
            operation.start()
            return DBusPath(path)
        } catch (e: Exception) {
            operation.abort()
            throw e
        }
    }

    private fun emitAdded(
        conn: DBusConnection?,
        path: String,
        interfaces: Map<String, Map<String, Variant<*>>>,
    ) {
        emitSignal(conn, ObjectManager.InterfacesAdded(ROOT_PATH, DBusPath(path), interfaces))
    }

    private fun emitRemoved(
        conn: DBusConnection?,
        path: String,
        interfaces: Collection<String>,
    ) {
        emitSignal(
            conn,
            ObjectManager.InterfacesRemoved(ROOT_PATH, DBusPath(path), interfaces.toList()),
        )
    }

    private fun emitProperties(
        path: String,
        interfaceName: String,
        changed: Map<String, Variant<*>>,
    ) {
        emitSignal(Properties.PropertiesChanged(path, interfaceName, changed, emptyList()))
    }

    private fun emitSignal(signal: DBusSignal) {
        emitSignal(connection, signal)
    }

    private fun emitSignal(conn: DBusConnection?, signal: DBusSignal) {
        if (conn == null) return
        runCatching { conn.sendMessage(signal) }
            .onFailure {
                logger.w { "D-Bus signal failed: ${it.message}" }
                markConnectionLost(conn)
            }
    }

    private fun watchPaths(): List<DBusPath> = synchronized(objectsLock) {
        watches.values.map { DBusPath(it.path) }
    }

    private fun managerCapabilities(): List<String> = primaryManagerCapabilities(
        classicAvailable = classicAvailable,
        concurrentWatches = libPebble.config.value.watchConfig.multipleConnectedWatchesSupported,
    )

    private fun platformHealth(snapshot: PlatformProviderSnapshot): Map<String, Variant<*>> {
        val state = if (snapshot.state == "starting") "degraded" else snapshot.state
        return buildMap {
            put("state", Variant(state))
            PLATFORM_DOMAINS.forEach { domain ->
                val domainState = when {
                    (snapshot.domains and domain.bit) != 0L -> "ready"
                    (snapshot.failedDomains and domain.bit) != 0L -> "failed"
                    (snapshot.degradedDomains and domain.bit) != 0L -> "degraded"
                    (snapshot.supportedDomains and domain.bit) != 0L -> when (snapshot.state) {
                        "failed" -> "failed"
                        "missing" -> "missing"
                        else -> "degraded"
                    }
                    else -> "missing"
                }
                put(domain.healthName, Variant(domainState))
            }
        }.toMap()
    }

    private fun platformCapabilities(snapshot: PlatformProviderSnapshot): List<String> = buildList {
        if (snapshot.state == "ready" || snapshot.state == "degraded") {
            add("platform.provider")
        }
        PLATFORM_DOMAINS.filter { (snapshot.domains and it.bit) != 0L }
            .forEach { if (it.capability !in this) add(it.capability) }
    }.toList()

    private fun watchObjectId(device: KnownPebbleDevice): String {
        return settings.watchObjectId(device.serial, device.identifier.asString)
    }

    private fun transportOf(device: PebbleDevice?): String = when (device?.identifier) {
        null -> "unknown"
        is PebbleBtClassicIdentifier -> "classic"
        else -> "ble"
    }

    private fun stopAllScans() {
        libPebble.stopBleScan()
        libPebble.stopClassicScan()
    }

    private fun connectionStateOf(device: PebbleDevice?): String = when {
        device is CommonConnectedDevice -> "connected"
        device is ConnectingPebbleDevice && device.negotiating -> "negotiating"
        device is ConnectingPebbleDevice -> "connecting"
        device?.connectionFailureInfo != null -> "failed"
        else -> "disconnected"
    }

    private fun sanitizedConnectionError(device: PebbleDevice?): String =
        if (device?.connectionFailureInfo == null) "" else ERROR_TRANSPORT_FAILED

    private fun screenshotRecords(): List<Map<String, Variant<*>>> =
        runCatching { screenshotStore.list().map(RockpoolScreenshotRecord::asVariantMap) }
            .onFailure { logger.w(it) { "could not list saved screenshots" } }
            .getOrDefault(emptyList())

    private fun writeFixedLogFile(source: File): File {
        require(source.isFile) { "watch log source is unavailable" }
        val home = System.getProperty("user.home")?.takeIf { it.isNotBlank() }
            ?: error("user home directory is unavailable")
        val downloads = File(home, "Downloads").toPath()
        if (!Files.exists(downloads, LinkOption.NOFOLLOW_LINKS)) {
            Files.createDirectories(downloads)
        }
        require(Files.isDirectory(downloads, LinkOption.NOFOLLOW_LINKS)) {
            "Downloads is not a directory"
        }
        require(!Files.isSymbolicLink(downloads)) { "Downloads must not be a symlink" }
        val temporary = Files.createTempFile(downloads, ".pebble-", ".log")
        val target = downloads.resolve("pebble.log")
        try {
            Files.copy(source.toPath(), temporary, StandardCopyOption.REPLACE_EXISTING)
            try {
                Files.move(
                    temporary,
                    target,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING,
                )
            } catch (_: AtomicMoveNotSupportedException) {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING)
            }
        } finally {
            Files.deleteIfExists(temporary)
        }
        return target.toFile()
    }

    private fun Map<String, Variant<*>>.string(name: String): String? =
        this[name]?.value as? String

    private fun record(vararg values: Pair<String, Variant<*>>): Map<String, Variant<*>> =
        linkedMapOf(*values)

    private fun settingId(value: String): String =
        UUID.nameUUIDFromBytes(value.toByteArray(Charsets.UTF_8)).toString().replace("-", "")

    private fun invalidProperty(interfaceName: String, propertyName: String): DBusExecutionException =
        DBusExecutionException("Unknown property $interfaceName.$propertyName").apply {
            setType(ERROR_INVALID_ARGUMENT)
        }

    private fun immutableProperty(interfaceName: String, propertyName: String): DBusExecutionException =
        DBusExecutionException("Read-only property $interfaceName.$propertyName").apply {
            setType(ERROR_INVALID_ARGUMENT)
        }

    private fun invalidInterface(interfaceName: String): DBusExecutionException =
        DBusExecutionException("Unknown interface $interfaceName").apply {
            setType(ERROR_INVALID_ARGUMENT)
        }

    companion object {
        private const val BUS_NAME = "org.rockpool"
        private const val ROOT_PATH = "/org/rockpool"
        private const val MANAGER_PATH = "$ROOT_PATH/Manager"
        private const val PLATFORM_PATH = "$ROOT_PATH/Platform"
        private const val WATCHES_PATH = "$ROOT_PATH/Watches"
        private const val OPERATIONS_PATH = "$ROOT_PATH/Operations"
        private const val API_VERSION = "1"
        private const val DAEMON_VERSION = "2.0.0-libpebble3d"
        private const val MAX_OPERATIONS = 128
        private const val LOCKER_LIMIT = 500
        private const val SETTINGS_LIST_SEPARATOR = "\u001f"
        private const val MAX_NOTIFICATION_FILTERS = 256
        private const val MAX_CANNED_GROUPS = 64
        private const val MAX_CANNED_RESPONSES = 64
        private const val MAX_SETTING_STRING = 512
        private const val UINT32_MAX = 4_294_967_295L

        private const val OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager"
        private const val MANAGER_INTERFACE = "org.rockpool.Manager1"
        private const val DISCOVERY_INTERFACE = "org.rockpool.Discovery1"
        private const val ACCOUNT_INTERFACE = "org.rockpool.Account1"
        private const val PLATFORM_INTERFACE = "org.rockpool.Platform1"
        private const val WATCH_INTERFACE = "org.rockpool.Watch1"
        private const val FIRMWARE_INTERFACE = "org.rockpool.Firmware1"
        private const val APPLICATIONS_INTERFACE = "org.rockpool.Applications1"
        private const val TIMELINE_INTERFACE = "org.rockpool.Timeline1"
        private const val NOTIFICATIONS_INTERFACE = "org.rockpool.Notifications1"
        private const val MESSAGING_INTERFACE = "org.rockpool.Messaging1"
        private const val HEALTH_INTERFACE = "org.rockpool.Health1"
        private const val PROFILES_INTERFACE = "org.rockpool.Profiles1"
        private const val SCREENSHOTS_INTERFACE = "org.rockpool.Screenshots1"
        private const val LOGS_INTERFACE = "org.rockpool.Logs1"
        private const val DEVELOPER_INTERFACE = "org.rockpool.Developer1"
        private const val OPERATION_INTERFACE = "org.rockpool.Operation1"

        private const val ERROR_INVALID_ARGUMENT = "org.rockpool.Error.InvalidArgument"
        private const val ERROR_NOT_SUPPORTED = "org.rockpool.Error.NotSupported"
        private const val ERROR_NOT_FOUND = "org.rockpool.Error.NotFound"
        private const val ERROR_NOT_CONNECTED = "org.rockpool.Error.NotConnected"
        private const val ERROR_PAIRING_FAILED = "org.rockpool.Error.PairingFailed"
        private const val ERROR_PROVIDER_UNAVAILABLE = "org.rockpool.Error.ProviderUnavailable"
        private const val ERROR_AUTHENTICATION_FAILED = "org.rockpool.Error.AuthenticationFailed"
        private const val ERROR_BUSY = "org.rockpool.Error.Busy"
        private const val ERROR_INTERNAL = "org.rockpool.Error.Internal"

        private val CONNECTION_TIMEOUT = 45.seconds
        private val PAIR_CLEANUP_TIMEOUT = 15.seconds
        private val DISCONNECT_TIMEOUT = 15.seconds
        private val BUS_HEALTH_INTERVAL = 10.seconds
        private val LOCKER_RETRY_DELAY = 5.seconds
        private val NOTIFICATION_FILTER_FIELDS = setOf("id", "source", "enabled", "name", "icon")
        private val CANNED_RESPONSE_FIELDS = setOf("id", "name", "values")
        private val HEALTH_SETTING_KEYS = setOf(
            "enabled",
            "age",
            "height",
            "gender",
            "weight",
            "moreActive",
            "sleepMore",
            "imperialUnits",
        )
        private val PROFILE_KEYS = setOf("connected", "disconnected")

        private val PLATFORM_DOMAINS = listOf(
            PlatformDomain(1L shl 0, "notifications", "platform.notifications"),
            PlatformDomain(1L shl 1, "messaging", "platform.messaging"),
            PlatformDomain(1L shl 2, "media", "platform.media"),
            PlatformDomain(1L shl 3, "calls", "platform.calls"),
            PlatformDomain(1L shl 4, "calendar", "platform.calendar"),
            PlatformDomain(1L shl 5, "contacts", "platform.contacts"),
            PlatformDomain(1L shl 6, "location", "platform.location"),
            PlatformDomain(1L shl 7, "time", "platform.time"),
            PlatformDomain(1L shl 8, "device-state", "platform.device-state"),
            PlatformDomain(1L shl 9, "profiles", "platform.profiles"),
        )
    }
}

/**
 * Linearizes primary pairing/watch-connection completion and cleanup by normalized address.
 *
 * Cleanup ownership deliberately remains address-scoped until [finishCleanup]. A replacement
 * cannot begin until the ordered libpebble3 goal has been cancelled and WatchManager confirms
 * that its transport attempt is gone, so stale cleanup cannot affect the replacement's goal.
 */
internal enum class PrimaryConnectionAttemptKind {
    PAIR,
    WATCH_CONNECT,
}

internal enum class PrimaryConnectionCleanupState {
    CLAIMED,
    RETIRING,
}

internal class PrimaryConnectionAttemptRegistry {
    internal class Attempt internal constructor(
        val address: String,
        val kind: PrimaryConnectionAttemptKind,
        val identifier: PebbleIdentifier?,
        private val cancel: () -> Unit,
    ) {
        fun cancelOperation() = cancel()
    }

    private enum class State {
        PENDING,
        CLEANING,
        RETIRING,
    }

    private data class Entry(
        val attempt: Attempt,
        val state: State,
        val connectionRequested: Boolean = false,
        val retirementWaitStarted: Boolean = false,
    )

    private val lock = Any()
    private val entries = LinkedHashMap<String, Entry>()

    fun begin(
        address: String,
        kind: PrimaryConnectionAttemptKind,
        identifier: PebbleIdentifier? = null,
        cancel: () -> Unit,
    ): Attempt? = synchronized(lock) {
        val key = normalize(address)
        if (entries[key] != null) return@synchronized null
        Attempt(address, kind, identifier, cancel).also {
            entries[key] = Entry(it, State.PENDING)
        }
    }

    /**
     * Publish the libpebble3 goal while the attempt is still pending.
     *
     * Holding the registry monitor through [request] makes this a defined race with cleanup: a
     * cleanup which wins first suppresses the request, while a request which wins first is marked
     * for mandatory retirement before cleanup can proceed.
     */
    fun requestConnectIfPending(attempt: Attempt, request: () -> Unit): Boolean =
        synchronized(lock) {
            val key = normalize(attempt.address)
            val entry = entries[key]
            if (entry?.attempt !== attempt || entry.state != State.PENDING) {
                false
            } else {
                entries[key] = entry.copy(connectionRequested = true)
                request()
                true
            }
        }

    /** Release an admitted operation which was cancelled before it published a goal. */
    fun abandonPending(attempt: Attempt): Boolean = synchronized(lock) {
        val key = normalize(attempt.address)
        val entry = entries[key]
        if (
            entry?.attempt !== attempt || entry.state != State.PENDING ||
            entry.connectionRequested
        ) {
            false
        } else {
            entries.remove(key)
            true
        }
    }

    fun complete(attempt: Attempt): Boolean = synchronized(lock) {
        val key = normalize(attempt.address)
        val entry = entries[key]
        if (entry?.attempt !== attempt || entry.state != State.PENDING) {
            false
        } else {
            entries.remove(key)
            true
        }
    }

    fun claimCleanup(attempt: Attempt): Boolean = synchronized(lock) {
        val key = normalize(attempt.address)
        val entry = entries[key]
        if (entry?.attempt !== attempt || entry.state != State.PENDING) {
            false
        } else {
            entries[key] = entry.copy(state = State.CLEANING)
            true
        }
    }

    fun claimAllCleanup(kind: PrimaryConnectionAttemptKind? = null): List<Attempt> =
        synchronized(lock) {
            entries.mapNotNull { (key, entry) ->
                if (kind != null && entry.attempt.kind != kind) return@mapNotNull null
                when (entry.state) {
                    State.PENDING -> {
                        entries[key] = entry.copy(state = State.CLEANING)
                        entry.attempt
                    }
                    State.CLEANING -> entry.attempt
                    State.RETIRING -> null
                }
            }
        }

    fun isCleaning(attempt: Attempt): Boolean = synchronized(lock) {
        val entry = entries[normalize(attempt.address)]
        entry?.attempt === attempt && entry.state == State.CLEANING
    }

    fun cleanupState(attempt: Attempt): PrimaryConnectionCleanupState? = synchronized(lock) {
        val entry = entries[normalize(attempt.address)]
        if (entry?.attempt !== attempt) return@synchronized null
        when (entry.state) {
            State.PENDING -> null
            State.CLEANING -> PrimaryConnectionCleanupState.CLAIMED
            State.RETIRING -> PrimaryConnectionCleanupState.RETIRING
        }
    }

    /** Retire any published goal before the address can be released to a replacement attempt. */
    fun retireConnectionIfCleaning(attempt: Attempt, retire: () -> Unit): Boolean =
        synchronized(lock) {
            val key = normalize(attempt.address)
            val entry = entries[normalize(attempt.address)]
            if (entry?.attempt !== attempt || entry.state != State.CLEANING) {
                false
            } else {
                try {
                    if (entry.connectionRequested) retire()
                } finally {
                    val current = entries[key]
                    if (current?.attempt === attempt && current.state == State.CLEANING) {
                        entries[key] = current.copy(state = State.RETIRING)
                    }
                }
                true
            }
        }

    /** Only one bounded waiter/reaper owns retirement acknowledgement for an attempt. */
    fun claimRetirementWait(attempt: Attempt): Boolean = synchronized(lock) {
        val key = normalize(attempt.address)
        val entry = entries[key]
        if (
            entry?.attempt !== attempt || entry.state != State.RETIRING ||
            entry.retirementWaitStarted
        ) {
            false
        } else {
            entries[key] = entry.copy(retirementWaitStarted = true)
            true
        }
    }

    fun finishCleanup(attempt: Attempt) {
        synchronized(lock) {
            val key = normalize(attempt.address)
            val entry = entries[key]
            if (entry?.attempt === attempt && entry.state == State.RETIRING) {
                entries.remove(key)
            }
        }
    }

    private fun normalize(address: String): String = address.lowercase()
}
