// Distinct facade name: commonMain/main.kt (the getPlatform/performPlatformSpecificInit expects)
// already compiles to io.rebble.libpebblecommon.MainKt in libpebble3's jvm jar, so the daemon
// entrypoint must not also be MainKt or the two collide on the classpath.
@file:JvmName("Daemon")

package io.rebble.libpebblecommon

import co.touchlab.kermit.Logger
import co.touchlab.kermit.Severity
import io.rebble.libpebblecommon.connection.AppContext
import io.rebble.libpebblecommon.connection.DiscoveredPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.LibPebble3
import io.rebble.libpebblecommon.rockpool.LibPebble3Service
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.rockpool.LegacyRockpooldImporter
import io.rebble.libpebblecommon.rockpool.LegacyGlobalSettingsReconciler
import io.rebble.libpebblecommon.rockpool.LibPebbleConfigMutationCoordinator
import io.rebble.libpebblecommon.rockpool.PlatformProviderController
import io.rebble.libpebblecommon.rockpool.PlatformNotificationBackend
import io.rebble.libpebblecommon.rockpool.NotificationFilterCoordinator
import io.rebble.libpebblecommon.rockpool.ProfileSwitchCoordinator
import io.rebble.libpebblecommon.rockpool.ProfileSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.PrimaryCannedResponsesReconciler
import io.rebble.libpebblecommon.rockpool.AccountSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.AccountIdentityCoordinator
import io.rebble.libpebblecommon.rockpool.HealthSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.JvmConfigStoragePolicy
import io.rebble.libpebblecommon.rockpool.AccountLockerUpgradeResult
import io.rebble.libpebblecommon.rockpool.AccountLockerUpgradeReconciler
import io.rebble.libpebblecommon.rockpool.TimelineWindowCoordinator
import io.rebble.libpebblecommon.rockpool.ACCOUNT_LOCKER_SESSION_GATE_COMPLETE
import io.rebble.libpebblecommon.rockpool.ACCOUNT_LOCKER_SESSION_GATE_MARKER
import io.rebble.libpebblecommon.rockpool.ACCOUNT_LOCKER_SESSION_GATE_RETIRED
import io.rebble.libpebblecommon.rockpool.initializeAccountLockerSessionGate
import io.rebble.libpebblecommon.rockpool.isAccountLockerSessionGateRetired
import io.rebble.libpebblecommon.rockpool.applyMandatoryDaemonConfigPolicy
import io.rebble.libpebblecommon.rockpool.createBluezBondedWatchForgetCoordinator
import io.rebble.libpebblecommon.rockpool.SailfishDeviceActivity
import io.rebble.libpebblecommon.rockpool.SailfishRfcommSocketFactory
import io.rebble.libpebblecommon.rockpool.SendTextConfigurationCoordinator
import io.rebble.libpebblecommon.rockpool.PlatformSystemMessaging
import io.rebble.libpebblecommon.rockpool.loadPlatformNotificationFilters
import io.rebble.libpebblecommon.rockpool.platformProviderModule
import io.rebble.libpebblecommon.ui.RockpoolUiService
import io.rebble.libpebblecommon.ui.rockpoolLogSeverity
import io.rebble.libpebblecommon.linux.web.RebbleBootConfigProvider
import io.rebble.libpebblecommon.linux.web.RebbleAccountIdentityProvider
import io.rebble.libpebblecommon.linux.web.RebbleTokenProvider
import io.rebble.libpebblecommon.linux.web.RebbleWebServices
import io.rebble.libpebblecommon.linux.voice.RebbleTranscriptionProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlin.time.Duration.Companion.seconds

/**
 * Headless Sailfish daemon entrypoint.  It uses the generic Linux libpebble3
 * services and the io.rebble.libpebble3 D-Bus control surface.  Sailfish-specific
 * platform access is deliberately supplied only by the external provider.
 */
fun main() {
    // A Sailfish session launcher withholds the privileged host until this
    // process is non-dumpable. Portable launches have no launcher socket and
    // continue normally with platform domains reported unavailable.
    PlatformProviderController.hardenProcess()

    val settings = RockpoolSettings()

    // Preserve the saved journal threshold across daemon restarts. Diagnostic environment flags
    // override that initial value, while the UI can still toggle debug output at runtime.
    Logger.setMinSeverity(
        when {
            System.getenv("LIBPEBBLE3D_VERBOSE") == "1" -> Severity.Verbose
            System.getenv("LIBPEBBLE3D_DEBUG") == "1" -> Severity.Debug
            else -> rockpoolLogSeverity(settings.getInt("logLevel", 1))
        }
    )
    Logger.i { "libpebble3 (Linux desktop) starting" }

    // Shared with RockpoolUiService: setOAuthToken (UI's boot.rebble.io login) lands here and
    // authenticates all Rebble web services.
    val oauthToken = { settings.get("account.oauthToken").ifEmpty { null } }

    // One boot-config fetch shared by every Rebble-backed feature: firmware/locker (web services),
    // the account dev token (getAccountToken), and the ASR voice endpoints.
    val bootConfig = RebbleBootConfigProvider(oauthToken)
    val webServices = RebbleWebServices(oauthToken, bootConfig)
    val accountIdentityProvider = RebbleAccountIdentityProvider(oauthToken, bootConfig)
    // Tracing hook: record ktor/TLS reflection in the native-image metadata (build-native.sh).
    if (System.getenv("LIBPEBBLE3D_TRACE_HTTP") == "1") {
        runBlocking { runCatching { webServices.selfTest() }.onFailure { it.printStackTrace() } }
    }

    val platformProvider = PlatformProviderController()
    val deviceActivity = SailfishDeviceActivity()
    val notificationBackend = PlatformNotificationBackend(
        platformProvider,
        loadPlatformNotificationFilters(settings),
        deviceActivity,
    )
    val timelineWindow = TimelineWindowCoordinator(settings)
    val rfcommSocketFactory = SailfishRfcommSocketFactory()

    val libPebble = LibPebble3.create(
        // Rockpool's public contract requires concurrent watches.  Its legacy developer API is
        // specifically the unauthenticated local port-9000 server, never the CloudPebble proxy.
        // Keep both policies explicit because libpebble3's mobile defaults differ.
        defaultConfig = LibPebbleConfig(
            watchConfig = WatchConfig(
                multipleConnectedWatchesSupported = true,
                lanDevConnection = true,
            ),
        ),
        webServices = webServices,
        appContext = AppContext(),
        // Backs Pebble.getAccountToken(): the Rebble account's user id (from users/me).
        tokenProvider = RebbleTokenProvider(oauthToken, bootConfig),
        proxyTokenProvider = MutableStateFlow(null),
        // Voice dictation via Rebble ASR (subscribers only).
        transcriptionProvider = RebbleTranscriptionProvider(bootConfig),
        platformOverrides = listOf(
            platformProviderModule(
                platformProvider,
                notificationBackend,
                deviceActivity,
                timelineWindow,
                rfcommSocketFactory,
            )
        ),
    )
    // The public facade deliberately remains LibPebble. Only the daemon consumes narrowly
    // additive concrete APIs for OS-bond import and absent-only legacy health initialization.
    val concreteLibPebble = libPebble as? LibPebble3
        ?: error("LibPebble3 factory returned an unsupported implementation")
    val bondedWatchImporter = concreteLibPebble::importBondedWatches
    val notificationFilters = NotificationFilterCoordinator(
        loadEntries = settings::entries,
        replacePrefixes = settings::replacePrefixes,
        replaceRuntimeFilters = notificationBackend::replaceFilters,
        updateMuteState = concreteLibPebble::persistNotificationAppMuteState,
        forgetApplication = libPebble::forgetNotificationApp,
        reconcileMuteStates = concreteLibPebble::reconcileNotificationAppMuteStates,
    )
    val accountSettings = AccountSettingsCoordinator(
        settings = settings,
        transitionAccount = libPebble::transitionAccount,
    )
    val accountIdentity = AccountIdentityCoordinator(
        accountSettings = accountSettings,
        fetchIdentity = accountIdentityProvider::get,
    )
    val profileSettings = ProfileSettingsCoordinator(settings)
    val healthSettings = HealthSettingsCoordinator(
        libPebble = libPebble,
        persistHealthSettings = concreteLibPebble::persistHealthSettings,
    )
    healthSettings.startObserving(CoroutineScope(SupervisorJob() + Dispatchers.Default))
    val configMutations = LibPebbleConfigMutationCoordinator.forLibPebble(libPebble)
    val configStoragePolicy = JvmConfigStoragePolicy(concreteLibPebble::serializedConfigLength)
    val cannedResponses = PrimaryCannedResponsesReconciler(
        settings = settings,
        configMutations = configMutations,
        configFitsStorage = configStoragePolicy::canAdmitCannedResponses,
    )
    val legacyImporter = LegacyRockpooldImporter(settings)
    val legacyGlobalSettings = LegacyGlobalSettingsReconciler(
        settings = settings,
        configMutations = configMutations,
        configGeneratedFromDefault = concreteLibPebble.configGeneratedFromDefault,
        initializeHealthSettingsIfAbsent = { transform ->
            healthSettings.initializeIfAbsent(
                transform = transform,
                initialize = concreteLibPebble::initializeHealthSettingsIfAbsent,
            )
        },
    )
    val sendTextConfiguration = SendTextConfigurationCoordinator(
        replaceConfiguration = libPebble::replaceSendTextConfiguration,
        systemMessaging = PlatformSystemMessaging(platformProvider),
        settings = settings,
        scope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
    )
    val bondedWatchForget = createBluezBondedWatchForgetCoordinator()
    libPebble.init()
    // Existing libpebble3 config wins for user preferences except the mandatory multi-watch
    // policy and the compatibility API's fixed local developer transport.  In particular, do
    // not let a mobile-app setting turn legacy SetDevConnEnabled into a cloud connection while
    // DevConnCloudEnabled truthfully remains unsupported.
    if (!applyMandatoryDaemonConfigPolicy(configMutations, configStoragePolicy)) {
        Logger.e {
            "mandatory daemon config exceeds storage capacity; continuing with stored policy"
        }
    }
    // LIBPEBBLE3D_PPOG_VERBOSE=1 logs every PPoGATT packet (inbound acks/data, outbound data)
    // for transport-level diagnosis. Apply it before canonical canned-response replay so that
    // admission measures the final startup config. A pre-existing oversized record must not turn
    // this diagnostic switch into a daemon startup failure.
    run {
        val wantPpogVerbose = System.getenv("LIBPEBBLE3D_PPOG_VERBOSE") == "1"
        var rejected = false
        configMutations.mutate { current ->
            if (current.bleConfig.verbosePpogLogging == wantPpogVerbose) {
                current
            } else {
                val updated = current.copy(
                    bleConfig = current.bleConfig.copy(verbosePpogLogging = wantPpogVerbose),
                )
                if (configStoragePolicy.canPersist(updated)) {
                    updated
                } else {
                    rejected = true
                    current
                }
            }
        }
        if (rejected) {
            Logger.w { "PPoGATT verbosity change exceeds config storage capacity; ignored" }
        }
        if (libPebble.config.value.bleConfig.verbosePpogLogging) {
            Logger.i { "PPoGATT verbose logging enabled" }
        }
    }
    runBlocking {
        legacyImporter.importIfNeeded(libPebble)
        if (!notificationFilters.reconcilePersistedState()) {
            Logger.w { "notification filter reconciliation is still pending" }
        }
        legacyGlobalSettings.reconcileIfNeeded(legacyImporter.isOriginalImportComplete())
        cannedResponses.reconcile()
        if (!sendTextConfiguration.reconcile()) {
            Logger.w { "Send Text configuration reconciliation is still pending" }
        }
        timelineWindow.reloadPersisted()
    }
    // Migration can create notification policy after the backend's constructor snapshot. Start
    // the helper only after that policy and libpebble's durable mute rows have been reconciled.
    platformProvider.start()
    val retireAccountLockerUpgradeIfNeeded: suspend () -> AccountLockerUpgradeResult = {
        initializeAccountLockerSessionGate(
            isInitialized = {
                isAccountLockerSessionGateRetired(
                    settings.get(ACCOUNT_LOCKER_SESSION_GATE_MARKER),
                )
            },
            markRetired = {
                settings.setChecked(
                    ACCOUNT_LOCKER_SESSION_GATE_MARKER,
                    ACCOUNT_LOCKER_SESSION_GATE_RETIRED,
                )
            },
            transitionAccount = libPebble::transitionAccount,
        )
    }
    val accountLockerUpgrade = runBlocking { retireAccountLockerUpgradeIfNeeded() }
    val accountLockerUpgradeReconciler = AccountLockerUpgradeReconciler(
        currentToken = { settings.get("account.oauthToken") },
        markerState = { settings.get(ACCOUNT_LOCKER_SESSION_GATE_MARKER) },
        markRetired = {
            settings.setChecked(
                ACCOUNT_LOCKER_SESSION_GATE_MARKER,
                ACCOUNT_LOCKER_SESSION_GATE_RETIRED,
            )
        },
        markComplete = {
            settings.setChecked(
                ACCOUNT_LOCKER_SESSION_GATE_MARKER,
                ACCOUNT_LOCKER_SESSION_GATE_COMPLETE,
            )
        },
        requestSync = libPebble::requestLockerSync,
    )
    accountSettings.addListener(accountLockerUpgradeReconciler::accountChanged)
    accountLockerUpgradeReconciler.start()
    if (accountLockerUpgrade == AccountLockerUpgradeResult.FAILED) {
        Logger.w { "account locker ownership upgrade is still pending" }
    }
    ProfileSwitchCoordinator(libPebble, settings).start()
    Logger.i { "libpebble3 init() complete; idling" }

    // The generic API owns io.rebble.libpebble3. The Rockpool UI facade owns org.rockpool on a
    // different non-shared connection and is intentionally best-effort: it must never delay
    // generic API startup if another Rockpool process temporarily owns the UI name.
    LibPebble3Service(
        libPebble,
        concreteLibPebble::requestConnectionImmediately,
        concreteLibPebble::cancelConnectionImmediately,
        concreteLibPebble::awaitConnectionRetired,
        bondedWatchImporter,
        bondedWatchForget,
        settings,
        platformProvider,
        notificationFilters,
        accountSettings,
        profileSettings,
        healthSettings,
        cannedResponses,
        sendTextConfiguration,
        rfcommSocketFactory.available,
    ).start()
    val rockpoolUiService = RockpoolUiService(
        libPebble,
        bondedWatchImporter,
        bondedWatchForget,
        settings,
        notificationFilters,
        accountSettings,
        profileSettings,
        healthSettings,
        timelineWindow,
        rfcommSocketFactory.available,
        configStoragePolicy::canPersist,
        sendTextConfiguration,
    )
    rockpoolUiService.start()
    accountIdentity.start()

    runBlocking {
        // BlueZ and libpebble's known-watch list can be unavailable during the
        // first user-session seconds.  Retain unmatched legacy state and retry
        // until it can be associated without unpairing anything.
        launch {
            while (true) {
                delay(30.seconds)
                var legacyRetryAttempted = false
                if (!legacyImporter.isComplete() || !legacyGlobalSettings.isComplete()) {
                    legacyImporter.importIfNeeded(libPebble)
                    legacyRetryAttempted = true
                    legacyGlobalSettings.reconcileIfNeeded(legacyImporter.isOriginalImportComplete())
                    timelineWindow.reloadPersisted()
                    rockpoolUiService.reloadWeatherSettings()
                }
                if (legacyRetryAttempted || notificationFilters.needsReconciliation()) {
                    notificationFilters.reconcilePersistedState()
                }
                if (!cannedResponses.isComplete()) cannedResponses.reconcile()
                sendTextConfiguration.reconcile()
                accountLockerUpgradeReconciler.recoverIfPending {
                    retireAccountLockerUpgradeIfNeeded() != AccountLockerUpgradeResult.FAILED
                }
            }
        }
        runAutoConnectHookIfEnabled(libPebble)
        awaitCancellation()
    }
}

/**
 * Device-testing hook until a real control surface (D-Bus API) exists:
 * LIBPEBBLE3D_AUTOCONNECT=1 starts a BLE scan after init, logs every watch the scan
 * surfaces, and connects to the first discovered Pebble — or only to the one whose BLE
 * address matches LIBPEBBLE3D_WATCH=AA:BB:CC:DD:EE:FF if that is set. Put the watch in
 * pairing mode (Settings > Bluetooth on the watch); bonding is handled by the JustWorks
 * agent.
 */
private fun CoroutineScope.runAutoConnectHookIfEnabled(libPebble: LibPebble) {
    if (System.getenv("LIBPEBBLE3D_AUTOCONNECT") != "1") return
    val wantedAddress = System.getenv("LIBPEBBLE3D_WATCH")?.uppercase()
    val logger = Logger.withTag("AutoConnect")

    launch {
        libPebble.connectionEvents.collect { logger.i { "connection event: $it" } }
    }
    launch {
        logger.i { "starting BLE scan (filter: ${wantedAddress ?: "first Pebble found"})" }
        libPebble.startBleScan()
        val attempted = mutableSetOf<String>()
        val logged = mutableSetOf<String>()
        libPebble.watches.collect { devices ->
            devices.forEach { device ->
                val address = device.identifier.asString.uppercase()
                if (logged.add("$address/${device::class.simpleName}")) {
                    logger.i { "watch: ${device.displayName()} $address (${device::class.simpleName})" }
                }
            }
            val candidate = devices.filterIsInstance<DiscoveredPebbleDevice>()
                .firstOrNull {
                    wantedAddress == null || it.identifier.asString.uppercase() == wantedAddress
                }
            if (candidate != null && attempted.add(candidate.identifier.asString)) {
                logger.i { "connecting to ${candidate.displayName()} (${candidate.identifier.asString})" }
                candidate.connect()
            }
        }
    }
}
