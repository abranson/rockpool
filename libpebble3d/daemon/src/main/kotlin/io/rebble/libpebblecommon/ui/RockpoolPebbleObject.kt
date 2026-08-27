package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.LibPebbleConfig
import io.rebble.libpebblecommon.connection.CommonConnectedDevice
import io.rebble.libpebblecommon.connection.ConnectedPebbleDevice
import io.rebble.libpebblecommon.connection.ConnectedPebbleDeviceInRecovery
import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckResult
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.PebbleDevice
import io.rebble.libpebblecommon.database.entity.MuteState
import io.rebble.libpebblecommon.disk.pbw.PbwApp
import io.rebble.libpebblecommon.disk.pbw.bestVariantFor
import io.rebble.libpebblecommon.js.PKJSApp
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.linux.web.RebbleAppstore
import io.rebble.libpebblecommon.packets.LogDump
import io.rebble.libpebblecommon.packets.ProtocolCapsFlag
import io.rebble.libpebblecommon.packets.blobdb.TimelineIcon
import io.rebble.libpebblecommon.protocolhelpers.PebblePacket
import io.rebble.libpebblecommon.rockpool.AccountSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.AccountTokenMutationResult
import io.rebble.libpebblecommon.rockpool.LibPebbleConfigMutationCoordinator
import io.rebble.libpebblecommon.rockpool.PRIMARY_CALENDAR_ENABLED_SETTING
import io.rebble.libpebblecommon.rockpool.PRIMARY_CALENDAR_PROVENANCE_EXPLICIT
import io.rebble.libpebblecommon.rockpool.PRIMARY_CALENDAR_PROVENANCE_SETTING
import io.rebble.libpebblecommon.rockpool.ProfileSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.RockpoolScreenshotStore
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.rockpool.SendTextConfigurationCoordinator
import io.rebble.libpebblecommon.rockpool.TimelineWindowCoordinator
import io.rebble.libpebblecommon.rockpool.encodeRockpoolPngRgba
import io.rebble.libpebblecommon.rockpool.loadPlatformNotificationFilters
import io.rebble.libpebblecommon.rockpool.mergePrimaryHealthSettings
import io.rebble.libpebblecommon.rockpool.rockpoolScreenshotPixelCount
import io.rebble.libpebblecommon.services.LogLevel
import io.rebble.libpebblecommon.services.appmessage.AppMessageData
import io.rebble.libpebblecommon.services.appmessage.AppMessageResult
import io.rebble.libpebblecommon.timeline.TimelineColor
import io.rebble.libpebblecommon.util.randomCookie
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.filterIsInstance
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.onSubscription
import kotlinx.coroutines.flow.takeWhile
import kotlinx.coroutines.flow.timeout
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withContext
import kotlinx.datetime.format
import kotlinx.datetime.format.DateTimeComponents
import kotlinx.io.files.Path
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.exceptions.DBusExecutionException
import org.freedesktop.dbus.types.UInt16
import org.freedesktop.dbus.types.Variant
import java.io.File
import java.nio.channels.Channels
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.OpenOption
import java.nio.file.Paths
import java.nio.file.SecureDirectoryStream
import java.nio.file.StandardOpenOption
import java.util.concurrent.atomic.AtomicLong
import kotlin.time.Duration.Companion.seconds
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Instant
import kotlin.uuid.Uuid

/**
 * One exported /org/rockpool/<ADDRESS> object per known watch. Backed by the LibPebble facade;
 * settings rockpool persisted itself (weather keys, health params, profiles...) go to
 * [RockpoolSettings] so the compatibility UI round-trips settings while the new API migrates
 * the corresponding domains to libpebble3 and the platform provider.
 */
internal class RockpoolPebbleObject(
    private val address: String,
    private val path: String,
    private val libPebble: LibPebble,
    private val settings: RockpoolSettings,
    private val notificationFilterMutations: RockpoolNotificationFilterMutations,
    private val accountSettings: AccountSettingsCoordinator,
    private val profileSettings: ProfileSettingsCoordinator,
    private val timelineWindow: TimelineWindowCoordinator,
    private val healthCoordinator: RockpoolHealthCoordinator,
    private val healthData: RockpoolHealthDataCoordinator = RockpoolHealthDataCoordinator(libPebble),
    private val weatherCoordinator: RockpoolWeatherCoordinator,
    private val notificationAppearance: RockpoolNotificationAppearanceCoordinator,
    private val scope: CoroutineScope,
    private val emit: (DBusSignal) -> Unit,
    private val broadcast: ((String) -> DBusSignal) -> Unit,
    private val configFitsStorage: (LibPebbleConfig) -> Boolean = { true },
    private val refreshWeather: () -> Unit = {},
    private val requestHealthData: suspend (ConnectedPebbleDevice) -> Boolean = { watch ->
        watch.requestHealthData(fullSync = false)
    },
    private val sendTextConfiguration: SendTextConfigurationCoordinator? = null,
) : RockpoolPebble {
    private val logger = Logger.withTag("RockpoolPebble")
    private val keyPrefix = address.replace(":", "_")
    private val watchLogDumpMutex = Mutex()
    private val appMessageMutex = Mutex()
    private val devConnectionToggleMutex = Mutex()
    private val devConnectionToggleGeneration = AtomicLong()
    private val healthSyncThrottle = RockpoolHealthSyncThrottle()
    private val screenshotStore by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        RockpoolScreenshotStore.forCurrentUser()
    }

    override fun getObjectPath(): String = path
    override fun isRemote(): Boolean = false

    private fun device(): PebbleDevice? = libPebble.watches.value.firstOrNull {
        it.identifier.asString.equals(address, ignoreCase = true)
    }

    private fun known(): KnownPebbleDevice? = device() as? KnownPebbleDevice

    // A recovery-mode watch is CommonConnectedDevice but NOT ConnectedPebbleDevice; it still
    // has watchInfo/battery/firmware/logs. connected() is only for what genuinely needs full
    // firmware (screenshots, language packs, dev connection).
    private fun commonConnected(): CommonConnectedDevice? = device() as? CommonConnectedDevice
    private fun connected(): ConnectedPebbleDevice? = device() as? ConnectedPebbleDevice

    private fun canMutateGlobalAppState(operation: String): Boolean {
        val knownAddresses = libPebble.watches.value.filterIsInstance<KnownPebbleDevice>()
            .map { it.identifier.asString }
        val allowed = rockpoolGlobalAppMutationAllowed(address, knownAddresses)
        if (!allowed) {
            logger.w {
                "$operation is unavailable with multiple known watches because " +
                    "libpebble3's locker is account-global"
            }
        }
        return allowed
    }

    private suspend fun sideloadAppForThisWatch(path: Path): Boolean {
        // Pbw installation enters the account-global locker and LibPebble then syncs every
        // connected watch. Recheck immediately before that call: another known watch may have
        // appeared while a download or PBW parse was in progress.
        if (!canMutateGlobalAppState("SideloadApp")) return false
        val watch = connected() ?: return false
        val pbw = PbwApp(path)
        if (pbw.bestVariantFor(watch.watchType.watchType) == null) {
            logger.w { "PBW has no compatible build for ${watch.watchType.watchType}" }
            return false
        }
        return libPebble.sideloadApp(path)
    }

    private fun key(name: String) = "$keyPrefix.$name"

    // The UI passes Qt.resolvedUrl() results ("file:///home/..."); rockpoold stripped the
    // scheme, so mirror that (URI also percent-decodes).
    private fun localPath(fileOrUrl: String): String = runCatching {
        if (fileOrUrl.startsWith("file:")) java.net.URI(fileOrUrl).path else fileOrUrl
    }.getOrDefault(fileOrUrl)

    private suspend fun writeLogDump(watch: ConnectedPebbleDevice): java.nio.file.Path {
        val home = System.getProperty("user.home")?.takeIf { it.isNotBlank() }
            ?: error("User home directory is unavailable")
        val homePath = Paths.get(home)
        require(homePath.isAbsolute && Files.isDirectory(homePath, LinkOption.NOFOLLOW_LINKS)) {
            "User home is not a directory"
        }

        val downloadsName = Paths.get(DOWNLOADS_DIRECTORY)
        val downloadsPath = homePath.resolve(downloadsName)
        if (!Files.exists(downloadsPath, LinkOption.NOFOLLOW_LINKS)) {
            try {
                Files.createDirectory(downloadsPath)
            } catch (_: FileAlreadyExistsException) {
                // A concurrent creator won the race; validate it through the directory handle below.
            }
        }

        Files.newDirectoryStream(homePath).use { homeDirectory ->
            @Suppress("UNCHECKED_CAST")
            val secureHome = homeDirectory as? SecureDirectoryStream<java.nio.file.Path>
                ?: error("Secure directory access is unavailable")
            secureHome.newDirectoryStream(downloadsName, LinkOption.NOFOLLOW_LINKS).use { downloads ->
                val targetName = Paths.get(LOG_DUMP_FILE)
                val temporaryName =
                    Paths.get(".$LOG_DUMP_FILE.${java.util.UUID.randomUUID()}.tmp")
                var temporaryExists = false
                try {
                    val options = setOf<OpenOption>(
                        StandardOpenOption.CREATE_NEW,
                        StandardOpenOption.WRITE,
                        LinkOption.NOFOLLOW_LINKS,
                    )
                    downloads.newByteChannel(temporaryName, options).use { channel ->
                        temporaryExists = true
                        Channels.newOutputStream(channel).bufferedWriter().use { output ->
                            writeWatchLogs(watch, output)
                        }
                    }

                    try {
                        downloads.move(temporaryName, downloads, targetName)
                    } catch (_: FileAlreadyExistsException) {
                        // Unix replaces the entry atomically. Retain a safe fallback for providers
                        // that require the existing target to be removed first.
                        downloads.deleteFile(targetName)
                        downloads.move(temporaryName, downloads, targetName)
                    }
                    temporaryExists = false
                } finally {
                    if (temporaryExists) {
                        runCatching { downloads.deleteFile(temporaryName) }
                    }
                }
            }
        }

        return downloadsPath.resolve(LOG_DUMP_FILE)
    }

    private suspend fun writeWatchLogs(watch: ConnectedPebbleDevice, output: Appendable) {
        output.appendLine("# Device logs:")
        val infiniteLogDump =
            watch.watchInfo.capabilities.contains(ProtocolCapsFlag.SupportsInfiniteLogDump)
        val maxGenerations = if (infiniteLogDump) {
            MAX_LOG_GENERATIONS
        } else {
            LEGACY_LOG_GENERATIONS
        }
        var successiveTimeouts = 0
        for (generation in 0 until maxGenerations) {
            when (writeLogGeneration(watch, generation, output)) {
                LogGenerationResult.MORE -> successiveTimeouts = 0
                LogGenerationResult.DONE -> {
                    successiveTimeouts = 0
                    if (infiniteLogDump) {
                        break
                    }
                }
                LogGenerationResult.TIMEOUT -> {
                    if (++successiveTimeouts > LOG_TIMEOUTS_ALLOWED) {
                        logger.e { "Successive log dump timeouts; aborting" }
                        break
                    }
                }
            }
        }
    }

    private suspend fun writeLogGeneration(
        watch: ConnectedPebbleDevice,
        generation: Int,
        output: Appendable,
    ): LogGenerationResult {
        logger.d { "requestLogGeneration: $generation" }
        output.appendLine("=== Generation: $generation ===")
        val cookie = randomCookie()
        var noLogs = false
        val inboundMessages = watch.inboundMessages as? SharedFlow<PebblePacket>
            ?: error("Watch message flow does not support subscription callbacks")
        try {
            inboundMessages
                .onSubscription {
                    watch.sendPPMessage(
                        LogDump.RequestLogDump(
                            logGeneration = generation.toUByte(),
                            cookie = cookie,
                        )
                    )
                }
                .filterIsInstance<LogDump.ReceivedLogDumpMessage>()
                .filter { it.cookie.get() == cookie }
                .takeWhile {
                    when (it) {
                        is LogDump.LogLine -> true
                        is LogDump.Done -> false
                        is LogDump.NoLogs -> {
                            noLogs = true
                            false
                        }
                    }
                }
                .timeout(LOG_RECEIVE_TIMEOUT)
                .collect {
                    if (it is LogDump.LogLine) {
                        val level = LogLevel.fromCode(it.level.get()).str
                        val timestamp = Instant.fromEpochSeconds(it.timestamp.get().toLong())
                            .format(DateTimeComponents.Formats.ISO_DATE_TIME_OFFSET)
                        output.appendLine(
                            "$level $timestamp ${it.filename.get()}:${it.line.get()}> " +
                                it.messageText.get()
                        )
                    }
                }
        } catch (_: TimeoutCancellationException) {
            // Flow.timeout leaves the parent coroutine active; the overall dump timeout does not.
            currentCoroutineContext().ensureActive()
            logger.w { "Timeout receiving logs for generation $generation" }
            output.appendLine("!!! Timeout receiving logs for this generation !!!")
            return LogGenerationResult.TIMEOUT
        }
        return if (noLogs) LogGenerationResult.DONE else LogGenerationResult.MORE
    }

    // ---- Identity / hardware ----
    override fun Address(): String = address
    override fun Name(): String = device()?.displayName() ?: address
    override fun SerialNumber(): String = known()?.serial ?: ""
    override fun PlatformString(): String =
        known()?.watchType?.revision ?: "unknown"

    override fun HardwarePlatform(): String =
        known()?.watchType?.watchType?.codename ?: "unknown"
    override fun SoftwareVersion(): String = known()?.runningFwVersion ?: ""
    override fun LanguageVersion(): String = rockpoolLanguageStatus(device()).version
    override fun Model(): Int = rockpoolWatchModel(
        connectedColor = commonConnected()?.watchInfo?.color,
        knownColor = known()?.color,
    )
    override fun IsConnected(): Boolean = commonConnected() != null
    override fun ConnectionState(): Int = connectionStateOf(device())
    override fun LastError(): String = device()?.connectionFailureInfo?.reason?.name ?: ""
    override fun Recovery(): Boolean = device() is ConnectedPebbleDeviceInRecovery

    // ---- Firmware / language packs ----
    private fun foundUpdate(): FirmwareUpdateCheckResult.FoundUpdate? =
        commonConnected()?.firmwareUpdateAvailable?.result
            as? FirmwareUpdateCheckResult.FoundUpdate

    override fun FirmwareUpgradeAvailable(): Boolean = foundUpdate() != null
    override fun CandidateFirmwareVersion(): String =
        foundUpdate()?.version?.stringVersion ?: ""

    override fun FirmwareReleaseNotes(): String = foundUpdate()?.notes ?: ""
    override fun PerformFirmwareUpgrade() {
        val update = foundUpdate() ?: return
        commonConnected()?.updateFirmware(update)
        emit(RockpoolPebble.UpgradingFirmwareChanged(path))
    }

    override fun UpgradingFirmware(): Boolean =
        commonConnected()?.firmwareUpdateState is FirmwareUpdater.FirmwareUpdateStatus.Active
    override fun LoadLanguagePack(pblFile: String) {
        val watch = connected() ?: run {
            logger.w { "LoadLanguagePack: watch is not connected" }
            return
        }
        val source = try {
            parseRockpoolLanguagePackSource(pblFile)
        } catch (e: IllegalArgumentException) {
            logger.w { "LoadLanguagePack: ${e.message}" }
            return
        }
        installRockpoolLanguagePack(
            source = source,
            installRemote = watch::installLanguagePack,
            installLocal = watch::installLanguagePack,
        )
    }

    // ---- Account / cloud sync (no Rebble web services wired up yet) ----
    override fun accountName(): String = settings.get("account.name")
    override fun accountEmail(): String = settings.get("account.email")
    override fun HasOAuthToken(): Boolean = settings.get("account.oauthToken").isNotEmpty()
    override fun setOAuthToken(token: String) {
        val mutation = accountSettings.enqueueTokenMutation(token)
        scope.launch {
            try {
                when (
                    mutation.execute(
                        beginCommit = { true },
                        persist = accountSettings::setToken,
                    )
                ) {
                    AccountTokenMutationResult.Saved -> {
                        // A fresh token can unlock the firmware update check (cohorts needs auth);
                        // force it so a previous auth-less result does not suppress the re-check.
                        commonConnected()?.checkforFirmwareUpdate(force = true)
                    }
                    AccountTokenMutationResult.Cancelled -> Unit
                    AccountTokenMutationResult.PersistenceFailed ->
                        logger.e { "Account credentials could not be saved" }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.e("Account credential update failed", e)
            }
        }.invokeOnCompletion { mutation.release() }
    }

    override fun syncAppsFromCloud(): Boolean = settings.getBool("account.syncAppsFromCloud")
    override fun setSyncAppsFromCloud(enable: Boolean) {
        settings.set("account.syncAppsFromCloud", enable)
        if (enable) libPebble.requestLockerSync()
    }

    override fun resetTimeline() {
        scope.launch {
            try {
                val watch = connected()
                if (watch == null) {
                    logger.w { "resetTimeline: addressed watch is not connected in normal mode" }
                } else if (!watch.resetTimeline()) {
                    logger.w { "resetTimeline: one or more watch databases could not be cleared" }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.w(e) { "resetTimeline failed" }
            }
        }
    }

    // ---- Timeline window ----
    override fun setTimelineWindow(start: Int, fade: Int, end: Int) {
        if (!timelineWindow.update(address, start, fade, end)) {
            logger.w { "failed to update timeline window" }
        }
    }

    override fun timelineWindowStart(): Int = timelineWindow.windowFor(address).value.pastDays
    override fun timelineWindowFade(): Int =
        timelineWindow.windowFor(address).value.notificationFadeSeconds
    override fun timelineWindowEnd(): Int = timelineWindow.windowFor(address).value.futureDays
    override fun insertTimelinePin(jsonPin: String) {
        logger.i { "insertTimelinePin: not supported" }
    }

    // ---- Notification filter ----
    override fun NotificationsFilter(): Map<String, Variant<*>> = runBlocking {
        val configured = loadPlatformNotificationFilters(settings)
        libPebble.notificationApps().first().associate { appWithCount ->
            val app = appWithCount.app
            val enabled = if (app.muteState == MuteState.Always) {
                0
            } else {
                configured[app.packageName]?.mode ?: 2
            }
            // Explicit a{sv} signature: dbus-java can't infer the D-Bus type of a raw Map
            // wrapped in a Variant ("Can't wrap LinkedHashMap in an unqualified Variant").
            app.packageName to Variant(
                mapOf(
                    "name" to Variant(app.name),
                    "icon" to Variant(""),
                    "enabled" to Variant(enabled),
                    // Current per-app overrides so the UI can show/edit them (empty = using the
                    // resolved default). A TimelineIcon.code and a TimelineColor.name respectively.
                    "iconCode" to Variant(app.iconCode ?: ""),
                    "colorName" to Variant(app.colorName ?: ""),
                ),
                "a{sv}",
            )
        }
    }

    override fun SetNotificationFilter(sourceId: String, enabled: Int) {
        notificationFilterMutations.enqueueSet(sourceId, enabled)
        // RockpoolUiService observes both the application Flow and coordinator changes. It emits
        // one account-global diff to every exported watch, including primary-API writes.
    }

    // Per-app notification appearance overrides. updateNotificationAppState replaces vibe/colour/
    // icon together, so read the current entry and change only the one field. Empty clears the
    // override (falls back to the resolved default). The service-wide notification-app observer
    // uses the existing filter signal as an invalidation after the combined state is published.
    override fun SetNotificationAppColor(sourceId: String, colorName: String) {
        scope.launch {
            if (!notificationAppearance.setColor(sourceId, colorName)) {
                logger.w { "SetNotificationAppColor: unknown source $sourceId" }
            }
        }
    }

    override fun SetNotificationAppIcon(sourceId: String, iconCode: String) {
        scope.launch {
            if (!notificationAppearance.setIcon(sourceId, iconCode)) {
                logger.w { "SetNotificationAppIcon: unknown source $sourceId" }
            }
        }
    }

    // rgb as "#RRGGBB" so QML can use it as a colour directly; name is the value the setters take.
    override fun TimelineColors(): List<Variant<*>> = TimelineColor.entries.map { c ->
        Variant(
            mapOf(
                "name" to Variant(c.name),
                "displayName" to Variant(c.displayName),
                "rgb" to Variant("#%06X".format(c.color and 0xFFFFFF)),
            ),
            "a{sv}",
        )
    }

    override fun TimelineIcons(): List<Variant<*>> = TimelineIcon.entries.map { i ->
        Variant(
            mapOf(
                "code" to Variant(i.code),
                "name" to Variant(i.name),
            ),
            "a{sv}",
        )
    }

    override fun ForgetNotificationFilter(sourceId: String) {
        notificationFilterMutations.enqueueForget(sourceId)
        // The service-wide observer emits enabled=-1 after the Room row disappears.
    }

    // ---- Canned responses / favorite contacts (persisted for the UI round-trip) ----
    override fun cannedResponses(): Map<String, Variant<*>> = storedList("canned")
    override fun setCannedResponses(cans: Map<String, Variant<*>>) {
        // ResponsesPage edits exactly one source at a time. The old platform adapter merged that
        // group into its existing map; treating this call as a complete replacement silently
        // discarded every other notification source's replies.
        val validated = validateStoredList(cans)
        validated["com.pebble.sendText"]?.let {
            try {
                sendTextConfiguration?.validateResponses(it)
            } catch (_: IllegalArgumentException) {
                throw failedCall("Send Text canned responses exceed firmware limits")
            }
        }
        if (!mergeStoredList("canned", validated)) {
            throw failedCall("Canned responses could not be saved")
        }
        if ("com.pebble.sendText" in validated) sendTextConfiguration?.requestReconcile()
    }
    override fun getCannedResponses(groups: List<String>): Map<String, Variant<*>> =
        storedList("canned").filterKeys { groups.isEmpty() || it in groups }

    override fun setFavoriteContacts(cans: Map<String, Variant<*>>) {
        val validated = validateStoredList(cans)
        try {
            sendTextConfiguration?.validateFavorites(validated)
        } catch (_: IllegalArgumentException) {
            throw failedCall("Favorite contacts contain invalid Send Text routes")
        }
        if (!storeList("contacts", validated)) {
            throw failedCall("Favorite contacts could not be saved")
        }
        sendTextConfiguration?.requestReconcile(favoritesChanged = true)
    }
    override fun getFavoriteContacts(names: List<String>): Map<String, Variant<*>> =
        storedList("contacts").filterKeys { names.isEmpty() || it in names }

    private fun storeList(kind: String, values: Map<String, List<String>>): Boolean {
        val prefix = "$kind."
        return settings.replacePrefix(prefix, encodeStoredList(prefix, values))
    }

    private fun mergeStoredList(kind: String, values: Map<String, List<String>>): Boolean {
        val prefix = "$kind."
        return settings.updatePrefixChecked(prefix) { stored ->
            encodeStoredList(prefix, decodeStoredValues(prefix, stored) + values)
        }
    }

    private fun encodeStoredList(
        prefix: String,
        values: Map<String, List<String>>,
    ): Map<String, String> = buildMap {
        validateNormalizedStoredList(values)
        put("${prefix}keys", values.keys.joinToString(SEP))
        values.forEach { (name, entries) ->
            put("$prefix$name", entries.joinToString(SEP))
        }
    }

    private fun validateStoredList(
        values: Map<String, Variant<*>>,
    ): Map<String, List<String>> = try {
        require(values.size <= MAX_STORED_LIST_GROUPS)
        values.mapValues { (_, variant) ->
            val collection = variant.value as? Collection<*>
                ?: throw IllegalArgumentException("stored list value must be an array")
            require(collection.size <= MAX_STORED_LIST_VALUES)
            collection.map { entry ->
                val value = (entry as? Variant<*>)?.value ?: entry
                value as? String
                    ?: throw IllegalArgumentException("stored list entry must be a string")
            }
        }.also(::validateNormalizedStoredList)
    } catch (_: IllegalArgumentException) {
        throw failedCall("Stored list contains invalid data")
    }

    private fun validateNormalizedStoredList(values: Map<String, List<String>>) {
        require(values.size <= MAX_STORED_LIST_GROUPS)
        values.forEach { (name, entries) ->
            require(validStoredListGroup(name, entries))
        }
    }

    private fun storedList(kind: String): Map<String, Variant<*>> {
        val prefix = "$kind."
        return decodeStoredList(prefix, settings.entries(prefix))
    }

    private fun decodeStoredList(
        prefix: String,
        stored: Map<String, String>,
    ): Map<String, Variant<*>> = decodeStoredValues(prefix, stored)
        .mapValues { (_, values) -> Variant(values, "as") }

    private fun decodeStoredValues(
        prefix: String,
        stored: Map<String, String>,
    ): Map<String, List<String>> = buildMap {
        val names = stored["${prefix}keys"].orEmpty()
            .split(SEP)
            .filter(String::isNotEmpty)
            .distinct()
        for (name in names) {
            if (size >= MAX_STORED_LIST_GROUPS) break
            val encoded = stored["$prefix$name"] ?: continue
            val entries = encoded.split(SEP).filter(String::isNotEmpty)
            if (validStoredListGroup(name, entries)) put(name, entries)
        }
    }

    private fun validStoredListGroup(name: String, entries: List<String>): Boolean =
        name.isNotBlank() &&
            name != STORED_LIST_INDEX &&
            name.length <= MAX_STORED_LIST_TEXT_LENGTH &&
            SEP !in name &&
            entries.size <= MAX_STORED_LIST_VALUES &&
            entries.all { entry ->
                entry.isNotEmpty() &&
                    entry.length <= MAX_STORED_LIST_TEXT_LENGTH &&
                    SEP !in entry
            }

    // ---- Voice ----
    override fun voiceSessionResult(dumpFile: String, sentences: List<Variant<*>>) {
        logger.d { "voiceSessionResult: not supported" }
    }

    // ---- Developer connection / logging ----
    override fun DevConnectionEnabled(): Boolean =
        commonConnected()?.devConnectionActive?.value ?: false
    override fun DevConnListenPort(): UInt16 = UInt16(9000)
    override fun DevConnectionState(): Boolean =
        commonConnected()?.devConnectionActive?.value ?: false

    override fun DevConnCloudEnabled(): Boolean = false
    override fun DevConnCloudState(): Boolean = false
    override fun SetDevConnEnabled(enabled: Boolean) {
        // The old backend deliberately never persisted this unauthenticated local backdoor.  A
        // generation makes detached D-Bus calls last-request-wins even if their coroutines start
        // in the opposite order.
        val generation = devConnectionToggleGeneration.incrementAndGet()
        scope.launch {
            devConnectionToggleMutex.withLock {
                if (generation != devConnectionToggleGeneration.get()) return@withLock
                try {
                    if (enabled) {
                        commonConnected()?.startDevConnection()
                    } else {
                        commonConnected()?.stopDevConnection()
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.e("dev connection toggle failed", e)
                }
            }
        }
    }

    override fun SetDevConnCloudEnabled(enabled: Boolean) {
        logger.d { "SetDevConnCloudEnabled: not supported" }
    }

    override fun SetDevConnListenPort(port: UInt16) {
        logger.d { "SetDevConnListenPort: not supported" }
    }

    // The legacy method names are retained for D-Bus compatibility.  Rockpool no longer creates
    // a second private service-log file: these simply enable/disable debug messages in the normal
    // process journal.  Watch log export remains the separate DumpLogs operation below.
    override fun startLogDump(): String {
        setJournalLogLevel(0)
        return ""
    }

    override fun stopLogDump(): String {
        setJournalLogLevel(1)
        return ""
    }

    override fun getLogDump(): String = ""

    override fun isLogDumping(): Boolean = getLogLevel() == 0

    override fun setLogLevel(level: Int) {
        setJournalLogLevel(level)
    }

    override fun getLogLevel(): Int = normalizedRockpoolLogLevel(settings.getInt("logLevel", 1))

    private fun setJournalLogLevel(level: Int) {
        val normalized = normalizedRockpoolLogLevel(level)
        settings.set("logLevel", normalized)
        Logger.setMinSeverity(rockpoolLogSeverity(normalized))
    }

    override fun DumpLogs(fileName: String) {
        // Keep the argument for Rockpool D-Bus compatibility, but never trust it as an output path.
        scope.launch {
            if (!watchLogDumpMutex.tryLock()) {
                emit(RockpoolPebble.LogsDumped(path, false))
                return@launch
            }
            val ok = try {
                // Recovery-mode watches do not expose the message flow needed for safe direct
                // streaming. Do not fall back to libpebble3's path-based cache handoff.
                val watch = connected()
                if (watch != null) {
                    val target = withTimeout(LOG_DUMP_TIMEOUT) {
                        writeLogDump(watch)
                    }
                    logger.i { "DumpLogs: wrote watch logs to $target" }
                    true
                } else {
                    false
                }
            } catch (e: Exception) {
                logger.e("DumpLogs failed", e)
                false
            } finally {
                watchLogDumpMutex.unlock()
            }
            emit(RockpoolPebble.LogsDumped(path, ok))
        }
    }

    // ---- Apps / watchfaces ----
    private suspend fun locker(): List<LockerWrapper> {
        val faces = libPebble.getLocker(AppType.Watchface, null, LOCKER_LIMIT).first()
        val apps = libPebble.getLocker(AppType.Watchapp, null, LOCKER_LIMIT).first()
        return faces + apps
    }

    private suspend fun installedLocker(): List<LockerWrapper> {
        val watchType = known()?.watchType?.watchType ?: return emptyList()
        return rockpoolInstalledApplications(locker(), watchType)
    }

    override fun InstallApp(id: String) {
        scope.launch {
            if (!canMutateGlobalAppState("InstallApp")) return@launch
            if (connected() == null) {
                logger.w { "InstallApp($id): watch is not connected" }
                return@launch
            }
            val ok = try {
                val app = RebbleAppstore.downloadPbw(id)
                val installed = app != null && sideloadAppForThisWatch(app.path)
                val uuid = app?.uuid
                if (installed && uuid != null) {
                    // Also add it to the Rebble locker so it becomes a real locker member with a
                    // timeline user_token, then refresh so getTimelineToken sees it. Best-effort:
                    // the sandbox-token path still covers apps that don't make it into the locker.
                    val token = settings.get("account.oauthToken").ifEmpty { null }
                    if (canMutateGlobalAppState("InstallApp") &&
                        RebbleAppstore.addToLocker(uuid, token)
                    ) {
                        // RequestLockerSync also works on account-global state; do not dispatch
                        // it if a second watch appeared during the service call above.
                        if (!canMutateGlobalAppState("InstallApp")) return@launch
                        libPebble.requestLockerSync()
                    }
                }
                installed
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.e("InstallApp failed", e)
                false
            }
            logger.i { "InstallApp($id): $ok" }
            emit(RockpoolPebble.InstalledAppsChanged(path))
        }
    }

    override fun SideloadApp(packageFile: String) {
        scope.launch {
            if (!canMutateGlobalAppState("SideloadApp")) return@launch
            if (connected() == null) {
                logger.w { "SideloadApp($packageFile): watch is not connected" }
                return@launch
            }
            val ok = try {
                sideloadAppForThisWatch(Path(localPath(packageFile)))
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.e("sideload failed", e)
                false
            }
            logger.i { "SideloadApp($packageFile): $ok" }
            emit(RockpoolPebble.InstalledAppsChanged(path))
        }
    }

    override fun InstalledAppIds(): List<String> = runBlocking {
        installedLocker().map { formatRockpoolAppUuid(it.properties.id) }
    }

    // 'av' with variant-wrapped maps, matching rockpoold's QVariantList (the UI's
    // parser depends on that wire shape — plain aa{sv} demarshals differently in Qt).
    override fun InstalledApps(): List<Variant<*>> = runBlocking {
        val watchType = known()?.watchType?.watchType ?: return@runBlocking emptyList()
        rockpoolApplicationRecords(locker(), watchType)
    }

    override fun RemoveApp(id: String) {
        scope.launch {
            if (!canMutateGlobalAppState("RemoveApp")) return@launch
            val application = resolveRockpoolInstalledApplication(installedLocker(), id)
            if (application == null) {
                logger.w { "RemoveApp: unknown id $id" }
                return@launch
            }
            if (application is LockerWrapper.SystemApp) {
                logger.w { "RemoveApp: refusing to remove system application $id" }
                return@launch
            }
            if (!canMutateGlobalAppState("RemoveApp")) return@launch
            if (!libPebble.removeApp(application.properties.id)) {
                logger.w { "RemoveApp: failed to remove $id" }
            }
        }
    }

    // A PKJS session exists while its app runs on the watch. The UI's normalised UUID has
    // braces + lowercase; libpebble3's is bare — compare on the bare hex.
    @Suppress("DEPRECATION")
    private fun pkjsSession(uuid: String): PKJSApp? {
        val wanted = uuid.trim('{', '}').lowercase()
        return connected()?.currentPKJSSession?.value
            ?.takeIf { it.uuid.toString().lowercase() == wanted }
    }

    // Launch the app if its JS isn't already running (config needs a live PKJS session), get
    // the URL the app hands back, and emit it. Non-blocking so the D-Bus thread stays free.
    override fun ConfigurationURL(uuid: String) {
        scope.launch {
            val application = resolveRockpoolInstalledApplication(installedLocker(), uuid)
            if (application == null) {
                logger.w { "ConfigurationURL($uuid): application is not installed on this watch" }
                return@launch
            }
            val appUuid = application.properties.id
            var session = pkjsSession(uuid)
            if (session == null) {
                val watch = connected()
                if (watch == null) {
                    logger.w { "ConfigurationURL($uuid): watch is not connected" }
                    return@launch
                }
                session = try {
                    withTimeoutOrNull<PKJSApp>(8.seconds) {
                        watch.launchApp(appUuid)
                        while (true) {
                            pkjsSession(appUuid.toString())?.let { return@withTimeoutOrNull it }
                            delay(250)
                        }
                        @Suppress("UNREACHABLE_CODE") error("configuration session loop ended")
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "ConfigurationURL($uuid): application launch failed" }
                    null
                }
            }
            if (session == null) {
                logger.w { "ConfigurationURL($uuid): no PKJS session (app has no JS?)" }
                return@launch
            }
            val url = withTimeoutOrNull(15.seconds) {
                try {
                    session.requestConfigurationUrl()
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "ConfigurationURL($uuid): request failed" }
                    null
                }
            }
            if (url.isNullOrEmpty()) {
                logger.w { "ConfigurationURL($uuid): app returned no config URL" }
                return@launch
            }
            emit(RockpoolPebble.OpenURL(path, uuid, url))
        }
    }

    override fun ConfigurationClosed(uuid: String, result: String) {
        pkjsSession(uuid)?.triggerOnWebviewClosed(result)
            ?: logger.w { "ConfigurationClosed($uuid): no running PKJS session" }
    }

    override fun SetAppOrder(newList: List<String>) {
        scope.launch {
            if (!canMutateGlobalAppState("SetAppOrder")) return@launch
            val installed = installedLocker()
            val order = validateRockpoolAppOrder(newList, installed)
            if (order == null) {
                logger.w { "SetAppOrder: list does not match the applications on this watch" }
                return@launch
            }
            if (!canMutateGlobalAppState("SetAppOrder")) return@launch
            if (libPebble.setAppOrder(order)) {
                // The locker UUID flow does not change for an order-only mutation, so its
                // service-level observer cannot provide this legacy notification for us.
                emit(RockpoolPebble.InstalledAppsChanged(path))
            } else {
                logger.w { "SetAppOrder: atomic locker update was rejected" }
            }
        }
    }

    override fun SendAppData(uuid: String, data: Map<String, Variant<*>>) {
        val appUuid = try {
            parseRockpoolAppUuid(uuid)
        } catch (e: IllegalArgumentException) {
            throw invalidArgument(e.message ?: "Invalid AppMessage")
        }
        val watch = connected() ?: throw failedCall("Watch is not connected")
        val sessionKeys = pkjsSession(appUuid.toString())?.appInfo?.appKeys.orEmpty()
        val maximumPayloadSize = if (
            ProtocolCapsFlag.Supports8kAppMessage in watch.capabilities
        ) {
            MAX_8K_APP_MESSAGE_PAYLOAD
        } else {
            MAX_STANDARD_APP_MESSAGE_PAYLOAD
        }
        val dictionary = try {
            rockpoolAppMessageDictionary(data, sessionKeys, maximumPayloadSize)
        } catch (e: IllegalArgumentException) {
            throw invalidArgument(e.message ?: "Invalid AppMessage")
        }

        scope.launch {
            appMessageMutex.withLock {
                try {
                    val transactionId = watch.transactionSequence.next()
                    when (
                        watch.sendAppMessage(
                            AppMessageData(
                                transactionId = transactionId,
                                uuid = appUuid,
                                data = dictionary,
                            ),
                        )
                    ) {
                        is AppMessageResult.ACK ->
                            logger.d { "SendAppData($appUuid): transaction $transactionId ACKed" }

                        is AppMessageResult.NACK ->
                            logger.w { "SendAppData($appUuid): transaction $transactionId was NACKed" }
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "SendAppData($appUuid) failed" }
                }
            }
        }
    }

    private fun invalidArgument(message: String): DBusExecutionException =
        DBusExecutionException(message).apply {
            setType("org.freedesktop.DBus.Error.InvalidArgs")
        }

    private fun failedCall(message: String): DBusExecutionException =
        DBusExecutionException(message).apply {
            setType("org.freedesktop.DBus.Error.Failed")
        }

    override fun CloseApp(uuid: String) {
        scope.launch {
            val application = resolveRockpoolInstalledApplication(installedLocker(), uuid)
                ?: return@launch
            connected()?.let { watch ->
                try {
                    watch.stopApp(application.properties.id)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "CloseApp($uuid) failed" }
                }
            }
        }
    }

    override fun LaunchApp(uuid: String) {
        scope.launch {
            val application = resolveRockpoolInstalledApplication(installedLocker(), uuid)
                ?: return@launch
            connected()?.let { watch ->
                try {
                    watch.launchApp(application.properties.id)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "LaunchApp($uuid) failed" }
                }
            }
        }
    }

    // ---- Screenshots ----
    override fun RequestScreenshot() {
        scope.launch {
            try {
                val bitmap = connected()?.takeScreenshot() ?: run {
                    logger.w { "RequestScreenshot: not connected or unsupported" }
                    return@launch
                }
                val w = bitmap.width
                val h = bitmap.height
                val pixels = IntArray(rockpoolScreenshotPixelCount(w, h))
                bitmap.readPixels(pixels)
                currentCoroutineContext().ensureActive()
                val png = encodeRockpoolPngRgba(w, h, pixels)
                val captureContext = currentCoroutineContext()
                val record = withContext(Dispatchers.IO) {
                    screenshotStore.write(png) { captureContext.isActive }
                }
                emit(RockpoolPebble.ScreenshotAdded(path, record.path))
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.e("screenshot failed", e)
            }
        }
    }

    override fun Screenshots(): List<String> = try {
        // Preserve the compatibility API's historical ascending filename order.
        screenshotStore.list().sortedBy { it.id }.map { it.path }
    } catch (e: Exception) {
        logger.w(e) { "could not list screenshots" }
        emptyList()
    }

    override fun RemoveScreenshot(filename: String) {
        val removed = try {
            screenshotStore.remove(filename)
        } catch (e: Exception) {
            logger.w(e) { "could not remove screenshot" }
            null
        }
        if (removed != null) {
            emit(RockpoolPebble.ScreenshotRemoved(path, removed.path))
        }
    }

    // ---- Weather ----
    override fun setWeatherApiKey(key: String) = settings.set("weather.apiKey", key)
    override fun WeatherUnits(): String = settings.get("weather.units", "m")
    override fun setWeatherUnits(units: String) {
        if (units !in setOf("m", "e", "h")) throw invalidArgument("Unknown weather units")
        if (!settings.setChecked("weather.units", units)) {
            throw failedCall("Weather units could not be saved")
        }
        refreshWeather()
    }

    override fun WeatherLanguage(): String = settings.get("weather.language")
    override fun setWeatherLanguage(lang: String) {
        if (lang.isNotEmpty() && !WEATHER_LANGUAGE.matches(lang)) {
            throw invalidArgument("Unknown weather language")
        }
        if (!settings.setChecked("weather.language", lang)) {
            throw failedCall("Weather language could not be saved")
        }
    }

    override fun WeatherAltKey(): String = settings.get("weather.altKey")
    override fun setWeatherAltKey(key: String) = settings.set("weather.altKey", key)
    override fun WeatherLocations(): List<Variant<*>> = weatherCoordinator.locations()
    override fun SetWeatherLocations(locations: List<Variant<*>>) {
        val saved = try {
            weatherCoordinator.setLocations(locations)
        } catch (e: IllegalArgumentException) {
            throw invalidArgument(e.message ?: "Invalid weather locations")
        }
        if (!saved) throw failedCall("Weather locations could not be saved")
        val current = weatherCoordinator.locations()
        broadcast { targetPath -> RockpoolPebble.WeatherLocationsChanged(targetPath, current) }
        refreshWeather()
    }

    override fun InjectWeatherData(locationName: String, conditions: Map<String, Variant<*>>) {
        val saved = try {
            weatherCoordinator.inject(locationName, conditions)
        } catch (e: IllegalArgumentException) {
            throw invalidArgument(e.message ?: "Invalid weather data")
        }
        if (!saved) throw failedCall("Weather data could not be saved")
    }

    // ---- Health / units / profiles / calendar ----
    override fun HealthParams(): Map<String, Variant<*>> = try {
        rockpoolHealthParams(healthCoordinator.current())
    } catch (e: Exception) {
        throw failedCall("Health settings are unavailable")
    }

    override fun SetHealthParams(params: Map<String, Variant<*>>) {
        val normalized = try {
            normalizeRockpoolHealthParams(params)
        } catch (e: IllegalArgumentException) {
            throw invalidArgument(e.message ?: "Invalid health settings")
        }
        val update = try {
            healthCoordinator.updateWithResult(
                transform = { current -> mergePrimaryHealthSettings(current, normalized) },
                commit = { settings.setAllChecked(normalized) },
            )
        } catch (e: IllegalArgumentException) {
            throw invalidArgument(e.message ?: "Invalid health settings")
        } catch (e: Exception) {
            throw failedCall("Health settings are unavailable")
        }
        if (!update.saved) {
            throw failedCall("Health settings could not be saved")
        }
        // The old Health page immediately requested an incremental sync when the user enabled
        // tracking.  Use this exported object's device, rather than LibPebble's global request
        // (which chooses the first connected watch), and retain its one-minute request limit.
        if (!update.previous.trackingEnabled && update.current.trackingEnabled) {
            scope.launch {
                val watch = connected() ?: return@launch
                if (!healthSyncThrottle.tryAcquire()) return@launch
                try {
                    watch.requestHealthData(fullSync = false)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "health sync after enabling tracking failed" }
                }
            }
        }
    }

    override fun HealthOverview(): Map<String, Variant<*>> = try {
        runBlocking {
            withTimeout(HEALTH_OVERVIEW_TIMEOUT) { healthData.healthOverview() }
        }
    } catch (e: Exception) {
        logger.w(e) { "HealthOverview failed" }
        throw failedCall("Health history is unavailable")
    }

    override fun FetchHealthData() {
        val watch = connected() ?: throw failedCall("The addressed watch is not connected")
        if (!healthSyncThrottle.tryAcquire()) {
            throw failedCall("Health data was requested recently")
        }
        val accepted = try {
            runBlocking {
                withTimeout(HEALTH_FETCH_TIMEOUT) {
                    requestHealthData(watch)
                }
            }
        } catch (e: Exception) {
            logger.w(e) { "FetchHealthData failed" }
            false
        }
        if (!accepted) throw failedCall("The watch did not accept the health sync request")
    }

    override fun ImperialUnits(): Boolean = try {
        healthCoordinator.current().imperialUnits
    } catch (e: Exception) {
        throw failedCall("Health settings are unavailable")
    }

    override fun SetImperialUnits(imperial: Boolean) {
        val saved = try {
            healthCoordinator.update(
                transform = { current ->
                    mergePrimaryHealthSettings(
                        current,
                        mapOf("units.imperial" to imperial.toString()),
                    )
                },
                commit = { settings.setChecked("imperialUnits", imperial.toString()) },
            )
        } catch (e: Exception) {
            throw failedCall("Health settings are unavailable")
        }
        if (!saved) {
            throw failedCall("Unit setting could not be saved")
        }
    }

    override fun ProfileWhenConnected(): String = settings.get(key("profile.connected"))
    override fun SetProfileWhenConnected(profile: String) {
        if (!profileSettings.updateForWatch(known()?.serial, address, mapOf("connected" to profile))) {
            logger.w { "could not save connected profile setting" }
        }
    }

    override fun ProfileWhenDisconnected(): String = settings.get(key("profile.disconnected"))
    override fun SetProfileWhenDisconnected(profile: String) {
        if (
            !profileSettings.updateForWatch(
                known()?.serial,
                address,
                mapOf("disconnected" to profile),
            )
        ) {
            logger.w { "could not save disconnected profile setting" }
        }
    }

    override fun CalendarSyncEnabled(): Boolean =
        libPebble.config.value.watchConfig.calendarPins

    override fun SetCalendarSyncEnabled(enabled: Boolean) {
        var persisted = false
        var fitsStorage = true
        LibPebbleConfigMutationCoordinator.forLibPebble(libPebble).mutate { config ->
            val updated = config.copy(
                watchConfig = config.watchConfig.copy(calendarPins = enabled),
            )
            if (!configFitsStorage(updated)) {
                fitsStorage = false
                config
            } else {
                persisted = settings.setAllChecked(
                    mapOf(
                        PRIMARY_CALENDAR_ENABLED_SETTING to enabled.toString(),
                        PRIMARY_CALENDAR_PROVENANCE_SETTING to
                            PRIMARY_CALENDAR_PROVENANCE_EXPLICIT,
                    ),
                )
                if (persisted) updated else config
            }
        }
        if (!fitsStorage) {
            throw failedCall("Calendar setting exceeds config storage capacity")
        }
        if (!persisted) {
            throw failedCall("Calendar setting could not be saved")
        }
    }

    companion object {
        private const val STORED_LIST_INDEX = "keys"
        private const val MAX_STORED_LIST_GROUPS = 64
        private const val MAX_STORED_LIST_VALUES = 64
        private const val MAX_STORED_LIST_TEXT_LENGTH = 512
        private const val LOCKER_LIMIT = 500
        private const val DOWNLOADS_DIRECTORY = "Downloads"
        private const val LOG_DUMP_FILE = "pebble.log"
        private const val LEGACY_LOG_GENERATIONS = 4
        private const val MAX_LOG_GENERATIONS = 256
        private const val LOG_TIMEOUTS_ALLOWED = 2
        private const val MAX_STANDARD_APP_MESSAGE_PAYLOAD = 2_048
        private const val MAX_8K_APP_MESSAGE_PAYLOAD = 8_222
        private val HEALTH_OVERVIEW_TIMEOUT = 5.seconds
        private val HEALTH_FETCH_TIMEOUT = 12.seconds
        private val WEATHER_LANGUAGE = Regex("^[A-Z]{2}$")
        private val LOG_RECEIVE_TIMEOUT = 5.seconds
        private val LOG_DUMP_TIMEOUT = 2.minutes

        /** Unit separator for persisted string lists. */
        private const val SEP = "\u001f"
    }

    private enum class LogGenerationResult {
        MORE,
        DONE,
        TIMEOUT,
    }
}
