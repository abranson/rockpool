/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

/** Snapshot consumed by Platform1.  It contains no provider-controlled pointers or payloads. */
internal data class PlatformProviderSnapshot(
    val state: String,
    val provider: String = "",
    val abiVersion: String = "1.4",
    val buildId: String = "",
    val domains: Long = 0,
    val supportedDomains: Long = 0,
    val degradedDomains: Long = 0,
    val failedDomains: Long = 0,
    val helperPid: Long = 0,
    val error: String = "",
)

internal fun failedProviderStatusSnapshot(
    current: PlatformProviderSnapshot,
): PlatformProviderSnapshot = current.copy(
    state = "failed",
    domains = 0,
    degradedDomains = 0,
    failedDomains = current.supportedDomains,
    helperPid = 0,
    error = "org.rockpool.Error.ProviderUnavailable",
)

internal fun PlatformProviderSnapshot.isRestartOperational(): Boolean =
    state == "ready" || (state == "degraded" && helperPid > 0)

private fun PlatformProviderSnapshot.isRestartTerminal(): Boolean =
    state == "missing" || state == "failed"

internal suspend fun awaitPlatformProviderRestart(
    initial: PlatformProviderSnapshot,
    timeout: Duration,
    pollInterval: Duration,
    poll: suspend () -> PlatformProviderSnapshot,
): PlatformProviderSnapshot {
    var latest = initial
    if (latest.isRestartOperational() || latest.isRestartTerminal()) return latest
    return withTimeoutOrNull(timeout) {
        do {
            delay(pollInterval)
            latest = poll()
        } while (!latest.isRestartOperational() && !latest.isRestartTerminal())
        latest
    } ?: latest
}

/** Cross the Operation1 commit boundary immediately before native provider state is changed. */
internal suspend fun <T> runCommittedProviderRestart(
    beginCommit: () -> Boolean,
    restart: suspend () -> T,
): T? {
    if (!beginCommit()) return null
    return withContext(NonCancellable) { restart() }
}

internal sealed interface PlatformNotificationEvent {
    data class Posted(
        val flags: Int,
        val timestampMs: Long,
        val id: String,
        val replacesId: String,
        val applicationId: String,
        val applicationName: String,
        val title: String,
        val body: String,
        val category: String,
        val iconName: String,
    ) : PlatformNotificationEvent

    data class Closed(val id: String, val reason: Int) : PlatformNotificationEvent
}

internal data class PlatformCallEvent(
    val state: Int,
    val id: String,
    val name: String,
    val number: String,
)

internal data class PlatformLocation(
    val latitudeE7: Int,
    val longitudeE7: Int,
    val accuracyM: Int,
    val timestampMs: Long,
)

internal sealed interface PlatformLocationQueryResult {
    data class Success(val location: PlatformLocation) : PlatformLocationQueryResult
    data class Error(val status: Int) : PlatformLocationQueryResult
}

/**
 * Generic host-side lifecycle for a native platform provider.  The native
 * library scans only the fixed installation directory and validates ownership,
 * modes, ABI and uniqueness before it invokes a provider lifecycle method.
 */
internal class PlatformProviderController(
    initialSnapshot: PlatformProviderSnapshot = PlatformProviderSnapshot(
        state = "starting",
        error = "org.rockpool.Error.ProviderUnavailable",
    ),
    private val locationNativeAvailable: () -> Boolean = { nativeLibraryLoaded },
    private val locationStartNative: (Int, Int) -> LongArray =
        PlatformProviderNative::locationStart,
    private val locationCancelNative: (Long) -> Int =
        PlatformProviderNative::cancelLocation,
    private val locationDrainNative: () -> LongArray =
        PlatformProviderNative::drainLocationEvents,
) {
    private val logger = Logger.withTag("PlatformProvider")
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val lifecycleLock = Mutex()
    private val restartLock = Mutex()
    private val refreshStarted = AtomicBoolean(false)
    private val listeners = CopyOnWriteArrayList<(PlatformProviderSnapshot) -> Unit>()
    private val timeChangedListeners = CopyOnWriteArrayList<() -> Unit>()
    private val notificationListeners =
        CopyOnWriteArrayList<(PlatformNotificationEvent) -> Unit>()
    private val callListeners = CopyOnWriteArrayList<(PlatformCallEvent) -> Unit>()
    private val mediaListeners = CopyOnWriteArrayList<(Int?) -> Unit>()
    private val mediaLock = Any()
    private val pendingLocations = mutableMapOf<Long, CompletableDeferred<PlatformLocationQueryResult>>()

    private var latestMediaVolume: Int? = null

    @Volatile
    private var current = initialSnapshot

    fun snapshot(): PlatformProviderSnapshot = current

    fun addListener(listener: (PlatformProviderSnapshot) -> Unit) {
        listeners += listener
        runCatching { listener(current) }
            .onFailure { logger.w { "platform provider listener failed: ${it.message}" } }
    }

    fun addTimeChangedListener(listener: () -> Unit) {
        timeChangedListeners += listener
    }

    fun addNotificationListener(listener: (PlatformNotificationEvent) -> Unit) {
        notificationListeners += listener
    }

    fun addCallListener(listener: (PlatformCallEvent) -> Unit) {
        callListeners += listener
    }

    fun addMediaListener(listener: (Int?) -> Unit) {
        synchronized(mediaLock) {
            mediaListeners += listener
            latestMediaVolume?.let { volume ->
                runCatching { listener(volume) }.onFailure {
                    logger.w { "platform media listener failed: ${it.message}" }
                }
            }
        }
    }

    fun start() {
        scope.launch {
            lifecycleLock.withLock {
                update(startNative())
            }
        }
        if (refreshStarted.compareAndSet(false, true)) {
            scope.launch {
                while (true) {
                    delay(PLATFORM_STATUS_INTERVAL)
                    lifecycleLock.withLock {
                        if (nativeLibraryLoaded) {
                            update(statusNative())
                        }
                    }
                }
            }
            scope.launch {
                while (true) {
                    delay(PLATFORM_EVENT_INTERVAL)
                    lifecycleLock.withLock {
                        if (nativeLibraryLoaded) drainNativeEvents()
                    }
                }
            }
        }
    }

    suspend fun restart(beginCommit: () -> Boolean): PlatformProviderSnapshot? =
        withContext(Dispatchers.IO) {
            restartLock.withLock {
                // Waiting for another restart remains cancellable. Once stopNative() runs, finish
                // restoring a classified provider and report its actual result.
                runCommittedProviderRestart(beginCommit) {
                    val initial = lifecycleLock.withLock {
                        stopNative()
                        update(startNative())
                        current
                    }
                    awaitPlatformProviderRestart(
                        initial = initial,
                        timeout = PLATFORM_RESTART_TIMEOUT,
                        pollInterval = PLATFORM_RESTART_POLL_INTERVAL,
                    ) {
                        lifecycleLock.withLock {
                            if (nativeLibraryLoaded) update(statusNative())
                            current
                        }
                    }
                }
            }
        }

    suspend fun notificationCommand(
        command: Int,
        id: String,
        isCurrent: () -> Boolean,
    ): Int? =
        withContext(Dispatchers.IO) {
            lifecycleLock.withLock {
                if (!isCurrent()) return@withLock null
                if (!nativeLibraryLoaded || current.domains and NOTIFICATION_DOMAIN == 0L) {
                    return@withLock STATUS_UNAVAILABLE
                }
                runCatching { PlatformProviderNative.notificationCommand(command, id) }
                    .getOrElse {
                        logger.w { "platform notification command failed: ${it.message}" }
                        STATUS_UNAVAILABLE
                    }
            }
        }

    suspend fun replyMessage(
        id: String,
        text: String,
        isCurrent: () -> Boolean,
    ): Int? =
        withContext(Dispatchers.IO) {
            lifecycleLock.withLock {
                if (!isCurrent()) return@withLock null
                val requiredDomains = NOTIFICATION_DOMAIN or MESSAGING_DOMAIN
                if (!nativeLibraryLoaded || current.domains and requiredDomains != requiredDomains) {
                    return@withLock STATUS_UNAVAILABLE
                }
                runCatching { PlatformProviderNative.replyMessage(id, text) }
                    .getOrElse {
                        logger.w { "platform message reply failed: ${it.message}" }
                        STATUS_UNAVAILABLE
                    }
            }
        }

    suspend fun callCommand(
        command: Int,
        id: String,
        isCurrent: () -> Boolean,
    ): Int? =
        withContext(Dispatchers.IO) {
            lifecycleLock.withLock {
                if (!isCurrent()) return@withLock null
                if (!nativeLibraryLoaded || current.domains and CALLS_DOMAIN == 0L) {
                    return@withLock STATUS_UNAVAILABLE
                }
                runCatching { PlatformProviderNative.callCommand(command, id) }
                    .getOrElse {
                        logger.w { "platform call command failed: ${it.message}" }
                        STATUS_UNAVAILABLE
                    }
            }
        }

    suspend fun mediaCommand(command: Int, isCurrent: () -> Boolean): Int? =
        withContext(Dispatchers.IO) {
            lifecycleLock.withLock {
                if (!isCurrent()) return@withLock null
                if (!nativeLibraryLoaded || current.domains and MEDIA_DOMAIN == 0L) {
                    return@withLock STATUS_UNAVAILABLE
                }
                runCatching { PlatformProviderNative.mediaCommand(command) }
                    .getOrElse {
                        logger.w { "platform media command failed: ${it.message}" }
                        STATUS_UNAVAILABLE
                    }
            }
        }

    private fun update(snapshot: PlatformProviderSnapshot) {
        if (current == snapshot) return
        current = snapshot
        providerSnapshotChanged(snapshot)
        logger.i {
            "platform provider ${snapshot.state}" +
                snapshot.provider.takeIf { it.isNotEmpty() }?.let { ": $it" }.orEmpty()
        }
        listeners.forEach { listener ->
            runCatching { listener(snapshot) }
                .onFailure { logger.w { "platform provider listener failed: ${it.message}" } }
        }
    }

    internal fun providerSnapshotChanged(snapshot: PlatformProviderSnapshot) {
        synchronized(mediaLock) {
            if (snapshot.domains and MEDIA_DOMAIN == 0L && latestMediaVolume != null) {
                latestMediaVolume = null
                mediaListeners.forEach { listener ->
                    runCatching { listener(null) }.onFailure {
                        logger.w { "platform media listener failed: ${it.message}" }
                    }
                }
            }
        }
        if (snapshot.domains and LOCATION_DOMAIN == 0L) {
            retirePendingLocations(STATUS_UNAVAILABLE)
        }
    }

    suspend fun queryLocation(
        highAccuracy: Boolean,
        timeout: Duration,
    ): PlatformLocationQueryResult = withContext(Dispatchers.IO) {
        val timeoutMs = timeout.inWholeMilliseconds.coerceIn(1, LOCATION_TIMEOUT_MAX_MS.toLong())
            .toInt()
        val completion = CompletableDeferred<PlatformLocationQueryResult>()
        val requestId = lifecycleLock.withLock {
            if (!locationNativeAvailable() || current.domains and LOCATION_DOMAIN == 0L) {
                return@withContext PlatformLocationQueryResult.Error(STATUS_UNAVAILABLE)
            }
            val started = runCatching {
                locationStartNative(
                    if (highAccuracy) LOCATION_FINE else LOCATION_COARSE,
                    timeoutMs,
                )
            }.getOrElse {
                logger.w { "platform location query failed to start: ${it.message}" }
                return@withContext PlatformLocationQueryResult.Error(STATUS_UNAVAILABLE)
            }
            if (started.size != LOCATION_START_FIELD_COUNT ||
                started[0] !in STATUS_OK.toLong()..STATUS_INTERNAL_ERROR.toLong()) {
                return@withContext PlatformLocationQueryResult.Error(STATUS_PROTOCOL_ERROR)
            }
            val status = started[0].toInt()
            val id = started[1]
            if (status != STATUS_OK) {
                if (id != 0L) {
                    return@withContext PlatformLocationQueryResult.Error(STATUS_PROTOCOL_ERROR)
                }
                return@withContext PlatformLocationQueryResult.Error(status)
            }
            if (id <= 0L || pendingLocations.containsKey(id)) {
                return@withContext PlatformLocationQueryResult.Error(STATUS_PROTOCOL_ERROR)
            }
            pendingLocations[id] = completion
            id
        }

        try {
            withTimeoutOrNull(timeoutMs.milliseconds + LOCATION_COMPLETION_GRACE) {
                completion.await()
            } ?: PlatformLocationQueryResult.Error(STATUS_UNAVAILABLE)
        } catch (e: CancellationException) {
            throw e
        } finally {
            withContext(NonCancellable) {
                lifecycleLock.withLock {
                    if (pendingLocations.remove(requestId) === completion) {
                        runCatching { locationCancelNative(requestId) }
                            .onFailure {
                                logger.w { "platform location cancellation failed: ${it.message}" }
                            }
                    }
                }
            }
        }
    }

    private fun retirePendingLocations(status: Int) {
        if (pendingLocations.isEmpty()) return
        val pending = pendingLocations.values.toList()
        pendingLocations.clear()
        pending.forEach { it.complete(PlatformLocationQueryResult.Error(status)) }
    }

    /** Publish a helper-generation boundary before any fallible per-domain drain. */
    internal fun applyProviderGenerationBoundary(fieldEvents: List<List<Long>>) {
        if (fieldEvents.none { it[0] == PROVIDER_STATUS_EVENT.toLong() }) return
        // The proxy emitted a helper-generation boundary. Force every stateful
        // domain through unavailable before accepting events from the replacement,
        // even when status polling missed the restart and the new helper is ready.
        update(
            current.copy(
                state = "degraded",
                domains = 0,
                degradedDomains = current.supportedDomains,
                failedDomains = 0,
                helperPid = 0,
                error = "org.rockpool.Error.ProviderUnavailable",
            ),
        )
    }

    private fun startNative(): PlatformProviderSnapshot {
        if (!loadNativeLibrary()) {
            return PlatformProviderSnapshot(
                state = if (File(NATIVE_LOADER_PATH).isFile) "failed" else "missing",
                error = "org.rockpool.Error.ProviderUnavailable",
            )
        }
        return runCatching { decode(PlatformProviderNative.start()) }.getOrElse {
            logger.w { "platform loader start failed: ${it.message}" }
            PlatformProviderSnapshot(
                state = "failed",
                error = "org.rockpool.Error.ProviderUnavailable",
            )
        }
    }

    private fun stopNative() {
        if (nativeLibraryLoaded) {
            runCatching { decode(PlatformProviderNative.stop()) }
                .onSuccess(::update)
                .onFailure { logger.w { "platform loader stop failed: ${it.message}" } }
        }
    }

    private fun statusNative(): PlatformProviderSnapshot = runCatching {
        decode(PlatformProviderNative.status())
    }.getOrElse {
        logger.w { "platform loader status failed: ${it.message}" }
        failedProviderStatusSnapshot(current)
    }

    private fun drainNativeEvents() {
        val batchStarted = runCatching {
            PlatformProviderNative.beginEventBatch()
        }.getOrElse {
            logger.w { "platform event batch failed: ${it.message}" }
            return
        }
        if (!batchStarted) {
            logger.w { "platform event batch was already active" }
            return
        }
        try {
            val fields = runCatching { PlatformProviderNative.drainEvents() }.getOrElse {
                logger.w { "platform event drain failed: ${it.message}" }
                return
            }
            if (fields.size % EVENT_FIELD_COUNT != 0) {
                logger.w { "platform event drain returned malformed data" }
                return
            }
            val fieldEvents = fields.asList().chunked(EVENT_FIELD_COUNT)
            // drainEvents() consumes the generation marker. Publish it before
            // later JNI drains can fail so notification/action authority cannot
            // survive a helper replacement without its Reset barrier.
            applyProviderGenerationBoundary(fieldEvents)
            val notifications = runCatching {
                PlatformProviderNative.drainNotificationEvents()
            }.getOrElse {
                logger.w { "platform notification drain failed: ${it.message}" }
                return
            }
            val calls = runCatching { PlatformProviderNative.drainCallEvents() }.getOrElse {
                logger.w { "platform call drain failed: ${it.message}" }
                return
            }
            val media = runCatching { PlatformProviderNative.drainMediaEvents() }.getOrElse {
                logger.w { "platform media drain failed: ${it.message}" }
                return
            }
            val locations = runCatching { locationDrainNative() }.getOrElse {
                logger.w { "platform location drain failed: ${it.message}" }
                retirePendingLocations(STATUS_UNAVAILABLE)
                return
            }
            if (media.size % MEDIA_FIELD_COUNT != 0) {
                logger.w { "platform media drain returned malformed data" }
                return
            }
            if (locations.size % LOCATION_EVENT_FIELD_COUNT != 0) {
                logger.w { "platform location drain returned malformed data" }
                retirePendingLocations(STATUS_PROTOCOL_ERROR)
                return
            }
            if (fieldEvents.isNotEmpty() || notifications.isNotEmpty() ||
                calls.isNotEmpty() || media.isNotEmpty() || locations.isNotEmpty()) {
                // Provider callbacks cannot mutate the queues during this
                // batch. Refresh the authoritative domain mask before
                // dispatch; a concurrent disconnect has already cleared the
                // proxy's ready bits even if its reset callback is waiting.
                update(statusNative())
            }
            providerLocationEvents(locations)
            fieldEvents.forEach { event ->
                if (event[0] == PROVIDER_STATUS_EVENT.toLong()) return@forEach
                if (event[0] != TIME_CHANGED_EVENT.toLong() ||
                    event[2] !in
                        -MAX_UTC_OFFSET_SECONDS.toLong()..MAX_UTC_OFFSET_SECONDS.toLong() ||
                    event[3] !in 0L..1L) {
                    logger.w { "platform event drain returned an invalid time event" }
                    return@forEach
                }
                if (current.domains and TIME_DOMAIN == 0L) return@forEach
                timeChangedListeners.forEach { listener ->
                    runCatching(listener).onFailure {
                        logger.w { "platform time listener failed: ${it.message}" }
                    }
                }
            }
            notifications.forEach { record ->
                if (current.domains and NOTIFICATION_DOMAIN == 0L) return@forEach
                val event = decodeNotification(record) ?: run {
                    logger.w { "platform notification drain returned malformed data" }
                    return@forEach
                }
                notificationListeners.forEach { listener ->
                    runCatching { listener(event) }.onFailure {
                        logger.w { "platform notification listener failed: ${it.message}" }
                    }
                }
            }
            calls.forEach { record ->
                if (current.domains and CALLS_DOMAIN == 0L) return@forEach
                val event = decodeCall(record) ?: run {
                    logger.w { "platform call drain returned malformed data" }
                    return@forEach
                }
                callListeners.forEach { listener ->
                    runCatching { listener(event) }.onFailure {
                        logger.w { "platform call listener failed: ${it.message}" }
                    }
                }
            }
            media.asList().chunked(MEDIA_FIELD_COUNT).forEach { event ->
                if (current.domains and MEDIA_DOMAIN == 0L) return@forEach
                val volumePercent = decodeMedia(event) ?: run {
                    logger.w { "platform media drain returned malformed data" }
                    return@forEach
                }
                providerMediaVolume(volumePercent)
            }
        } finally {
            runCatching { PlatformProviderNative.endEventBatch() }
                .onSuccess { ended ->
                    if (!ended) logger.w { "platform event batch ended on the wrong thread" }
                }
                .onFailure { logger.w { "platform event batch end failed: ${it.message}" } }
            }
    }

    internal fun decodeNotification(record: ByteArray): PlatformNotificationEvent? {
        if (record.size < NOTIFICATION_PREFIX_SIZE || record.u16(2) != 0) return null
        val type = record.u16(0)
        val flags = record.u32(4)?.toInt() ?: return null
        val timestampMs = record.i64(8) ?: return null
        val closeReason = record.u32(16)?.toInt() ?: return null
        var offset = NOTIFICATION_PREFIX_SIZE
        val values = ArrayList<String>(NOTIFICATION_STRING_COUNT)
        repeat(NOTIFICATION_STRING_COUNT) { index ->
            val length = record.u32(20 + index * 4) ?: return null
            if (length > NOTIFICATION_STRING_MAX_BYTES[index] ||
                length > Int.MAX_VALUE || length.toInt() > record.size - offset
            ) {
                return null
            }
            val bytes = record.copyOfRange(offset, offset + length.toInt())
            val text = runCatching {
                UTF8_DECODER.get().reset().decode(ByteBuffer.wrap(bytes)).toString()
            }.getOrNull() ?: return null
            if ('\u0000' in text) return null
            values += text
            offset += length.toInt()
        }
        if (offset != record.size) return null
        val id = values[0]
        if (!validNotificationId(id, allowEmpty = false)) {
            return null
        }
        return when (type) {
            NOTIFICATION_POSTED_EVENT -> {
                if (flags and NOTIFICATION_FLAGS.inv() != 0 || closeReason != 0 ||
                    !validNotificationId(values[1], allowEmpty = true) ||
                    values[2].isEmpty() || (values[4].isEmpty() && values[5].isEmpty())) {
                    return null
                }
                PlatformNotificationEvent.Posted(
                    flags = flags,
                    timestampMs = timestampMs,
                    id = id,
                    replacesId = values[1],
                    applicationId = values[2],
                    applicationName = values[3],
                    title = values[4],
                    body = values[5],
                    category = values[6],
                    iconName = values[7],
                )
            }
            NOTIFICATION_CLOSED_EVENT -> {
                if (flags != 0 || timestampMs != 0L || closeReason !in 0..4 ||
                    values.drop(1).any { it.isNotEmpty() }) {
                    return null
                }
                PlatformNotificationEvent.Closed(id, closeReason)
            }
            else -> null
        }
    }

    internal fun decodeCall(record: ByteArray): PlatformCallEvent? {
        if (record.size < CALL_PREFIX_SIZE || record.u16(0) != CALL_EVENT ||
            record.u16(2) != 0) {
            return null
        }
        val state = record.u32(4)?.toInt() ?: return null
        if (state !in CALL_ENDED..CALL_HELD) return null
        var offset = CALL_PREFIX_SIZE
        val values = ArrayList<String>(CALL_STRING_COUNT)
        repeat(CALL_STRING_COUNT) { index ->
            val length = record.u32(8 + index * 4) ?: return null
            if (length > CALL_STRING_MAX_BYTES[index] || length > Int.MAX_VALUE ||
                length.toInt() > record.size - offset) {
                return null
            }
            val bytes = record.copyOfRange(offset, offset + length.toInt())
            val text = runCatching {
                UTF8_DECODER.get().reset().decode(ByteBuffer.wrap(bytes)).toString()
            }.getOrNull() ?: return null
            if ('\u0000' in text) return null
            values += text
            offset += length.toInt()
        }
        if (offset != record.size || !validCallId(values[0]) ||
            (state == CALL_ENDED && (values[1].isNotEmpty() || values[2].isNotEmpty()))) {
            return null
        }
        return PlatformCallEvent(state, values[0], values[1], values[2])
    }

    internal fun providerMediaVolume(volumePercent: Int) {
        if (volumePercent !in MIN_VOLUME_PERCENT..MAX_VOLUME_PERCENT) return
        synchronized(mediaLock) {
            latestMediaVolume = volumePercent
            mediaListeners.forEach { listener ->
                runCatching { listener(volumePercent) }.onFailure {
                    logger.w { "platform media listener failed: ${it.message}" }
                }
            }
        }
    }

    internal fun decodeMedia(fields: List<Long>): Int? {
        if (fields.size != MEDIA_FIELD_COUNT ||
            fields[0] != MEDIA_SYSTEM_VOLUME.toLong() ||
            fields[1] !in MIN_VOLUME_PERCENT.toLong()..MAX_VOLUME_PERCENT.toLong()) {
            return null
        }
        return fields[1].toInt()
    }

    internal fun providerLocationEvents(fields: LongArray) {
        if (fields.size % LOCATION_EVENT_FIELD_COUNT != 0) {
            retirePendingLocations(STATUS_PROTOCOL_ERROR)
            return
        }
        fields.asList().chunked(LOCATION_EVENT_FIELD_COUNT).forEach { record ->
            val requestId = record[0]
            val completion = pendingLocations.remove(requestId) ?: return@forEach
            completion.complete(
                decodeLocation(record)
                    ?: PlatformLocationQueryResult.Error(STATUS_PROTOCOL_ERROR),
            )
        }
    }

    internal fun decodeLocation(fields: List<Long>): PlatformLocationQueryResult? {
        if (fields.size != LOCATION_EVENT_FIELD_COUNT || fields[0] <= 0L ||
            fields[1] !in STATUS_OK.toLong()..STATUS_INTERNAL_ERROR.toLong()) {
            return null
        }
        val status = fields[1].toInt()
        if (status != STATUS_OK) {
            if (fields.drop(2).any { it != 0L }) return null
            return PlatformLocationQueryResult.Error(status)
        }
        val latitudeE7 = fields[2]
        val longitudeE7 = fields[3]
        val accuracyM = fields[4]
        val timestampMs = fields[5]
        if (latitudeE7 !in MIN_LATITUDE_E7.toLong()..MAX_LATITUDE_E7.toLong() ||
            longitudeE7 !in MIN_LONGITUDE_E7.toLong()..MAX_LONGITUDE_E7.toLong() ||
            accuracyM !in 0L..Int.MAX_VALUE.toLong() || timestampMs <= 0L) {
            return null
        }
        return PlatformLocationQueryResult.Success(
            PlatformLocation(
                latitudeE7 = latitudeE7.toInt(),
                longitudeE7 = longitudeE7.toInt(),
                accuracyM = accuracyM.toInt(),
                timestampMs = timestampMs,
            ),
        )
    }

    private fun validCallId(value: String): Boolean =
        value.isNotEmpty() && value.length <= CALL_ID_MAX &&
            value.all { it in 'A'..'Z' || it in 'a'..'z' || it in '0'..'9' || it == '_' }

    private fun validNotificationId(value: String, allowEmpty: Boolean): Boolean {
        if (value.isEmpty()) return allowEmpty
        if (value.length > NOTIFICATION_ID_MAX || value.any { it !in '0'..'9' }) return false
        return value.toULongOrNull()?.let { it in 1uL..UInt.MAX_VALUE.toULong() } == true
    }

    private fun ByteArray.u16(offset: Int): Int {
        if (offset < 0 || offset + 2 > size) return -1
        return (this[offset].toInt() and 0xff) or
            ((this[offset + 1].toInt() and 0xff) shl 8)
    }

    private fun ByteArray.u32(offset: Int): Long? {
        if (offset < 0 || offset + 4 > size) return null
        var value = 0L
        repeat(4) { index ->
            value = value or ((this[offset + index].toLong() and 0xffL) shl (index * 8))
        }
        return value
    }

    private fun ByteArray.i64(offset: Int): Long? {
        if (offset < 0 || offset + 8 > size) return null
        var value = 0L
        repeat(8) { index ->
            value = value or ((this[offset + index].toLong() and 0xffL) shl (index * 8))
        }
        return value
    }

    private fun decode(fields: Array<String>): PlatformProviderSnapshot {
        fun field(index: Int): String = fields.getOrNull(index).orEmpty()
        return PlatformProviderSnapshot(
            state = field(0).ifEmpty { "failed" },
            provider = field(1),
            buildId = field(2),
            abiVersion = field(3).ifEmpty { "1.4" },
            domains = field(4).toLongOrNull() ?: 0,
            helperPid = field(5).toLongOrNull() ?: 0,
            error = field(6),
            supportedDomains = field(7).toLongOrNull() ?: 0,
            degradedDomains = field(8).toLongOrNull() ?: 0,
            failedDomains = field(9).toLongOrNull() ?: 0,
        )
    }

    private fun loadNativeLibrary(): Boolean {
        return ensureNativeLibraryLoaded()
    }

    companion object {
        /**
         * Called before daemon initialization. The native side becomes
         * non-dumpable before acknowledging a package-owned session launcher;
         * a normal portable launch simply returns false and keeps platform
         * domains unavailable.
         */
        internal fun hardenProcess() {
            if (ensureNativeLibraryLoaded()) {
                runCatching { PlatformProviderNative.hardenProcess() }
            }
        }

        internal fun nativeLibraryAvailable(): Boolean = ensureNativeLibraryLoaded()

        private fun ensureNativeLibraryLoaded(): Boolean {
            if (nativeLibraryLoadAttempted) return nativeLibraryLoaded
            synchronized(PlatformProviderController::class.java) {
                if (nativeLibraryLoadAttempted) return nativeLibraryLoaded
                nativeLibraryLoadAttempted = true
                nativeLibraryLoaded = runCatching {
                    val file = File(NATIVE_LOADER_PATH)
                    require(file.isFile && !file.isDirectory) {
                        "native platform loader is not installed"
                    }
                    System.load(file.absolutePath)
                    true
                }.onFailure {
                    libraryLogger.w { "platform loader unavailable: ${it.message}" }
                }.getOrDefault(false)
                return nativeLibraryLoaded
            }
        }

        // This lives beside the Native Image so neither the daemon nor a
        // provider accepts an environment-controlled shared-library path.
        private val libraryLogger = Logger.withTag("PlatformProvider")
        const val NATIVE_LOADER_PATH = "/usr/libexec/libpebble3d/libpebble3d-platform-loader.so"
        const val TIME_CHANGED_EVENT = 9
        const val PROVIDER_STATUS_EVENT = 12
        const val TIME_DOMAIN = 1L shl 7
        const val NOTIFICATION_DOMAIN = 1L
        const val MESSAGING_DOMAIN = 1L shl 1
        const val CALLS_DOMAIN = 1L shl 3
        const val MEDIA_DOMAIN = 1L shl 2
        const val LOCATION_DOMAIN = 1L shl 6
        const val NOTIFICATION_POSTED_EVENT = 2
        const val NOTIFICATION_CLOSED_EVENT = 3
        const val EVENT_FIELD_COUNT = 4
        const val MAX_UTC_OFFSET_SECONDS = 24 * 60 * 60
        const val STATUS_OK = 0
        const val STATUS_CANCELLED = 1
        const val STATUS_INVALID_ARGUMENT = 2
        const val STATUS_NOT_SUPPORTED = 3
        const val STATUS_BUSY = 4
        const val STATUS_UNAVAILABLE = 5
        const val STATUS_IO_ERROR = 6
        const val STATUS_PROTOCOL_ERROR = 7
        const val STATUS_INTERNAL_ERROR = 8
        const val NOTIFICATION_DISMISS = 1
        const val NOTIFICATION_OPEN = 2
        const val NOTIFICATION_HAS_DEFAULT_ACTION = 1
        const val NOTIFICATION_HAS_REPLY_ACTION = 1 shl 1
        const val NOTIFICATION_FLAGS =
            NOTIFICATION_HAS_DEFAULT_ACTION or NOTIFICATION_HAS_REPLY_ACTION
        const val NOTIFICATION_PREFIX_SIZE = 52
        const val NOTIFICATION_STRING_COUNT = 8
        const val NOTIFICATION_ID_MAX = 64
        const val CALL_EVENT = 5
        const val CALL_ENDED = 0
        const val CALL_RINGING = 1
        const val CALL_DIALING = 2
        const val CALL_ACTIVE = 3
        const val CALL_HELD = 4
        const val CALL_ANSWER = 1
        const val CALL_HANG_UP = 2
        const val CALL_SILENCE = 3
        const val CALL_PREFIX_SIZE = 20
        const val CALL_STRING_COUNT = 3
        const val CALL_ID_MAX = 128
        const val MEDIA_VOLUME_UP = 4
        const val MEDIA_VOLUME_DOWN = 5
        const val MEDIA_SYSTEM_VOLUME = 1
        const val MEDIA_FIELD_COUNT = 2
        const val MIN_VOLUME_PERCENT = 0
        const val MAX_VOLUME_PERCENT = 100
        const val LOCATION_COARSE = 1
        const val LOCATION_FINE = 2
        const val LOCATION_START_FIELD_COUNT = 2
        const val LOCATION_EVENT_FIELD_COUNT = 6
        const val LOCATION_TIMEOUT_MAX_MS = 30_000
        const val MIN_LATITUDE_E7 = -900_000_000
        const val MAX_LATITUDE_E7 = 900_000_000
        const val MIN_LONGITUDE_E7 = -1_800_000_000
        const val MAX_LONGITUDE_E7 = 1_800_000_000
        private val NOTIFICATION_STRING_MAX_BYTES = longArrayOf(
            NOTIFICATION_ID_MAX.toLong(),
            NOTIFICATION_ID_MAX.toLong(),
            256,
            256,
            512,
            4096,
            128,
            128,
        )
        private val CALL_STRING_MAX_BYTES = longArrayOf(CALL_ID_MAX.toLong(), 256, 256)
        val PLATFORM_STATUS_INTERVAL = 5.seconds
        val PLATFORM_EVENT_INTERVAL = 100.milliseconds
        val PLATFORM_RESTART_TIMEOUT = 10.seconds
        val PLATFORM_RESTART_POLL_INTERVAL = 100.milliseconds
        val LOCATION_COMPLETION_GRACE = 1.seconds
        private val UTF8_DECODER = ThreadLocal.withInitial {
            Charsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
        }

        @Volatile
        var nativeLibraryLoadAttempted = false

        @Volatile
        var nativeLibraryLoaded = false
    }
}

/** JNI entry points implemented by native/platform_loader.c. */
internal object PlatformProviderNative {
    @JvmStatic
    external fun hardenProcess(): Boolean

    @JvmStatic
    external fun start(): Array<String>

    @JvmStatic
    external fun stop(): Array<String>

    @JvmStatic
    external fun status(): Array<String>

    @JvmStatic
    external fun beginEventBatch(): Boolean

    @JvmStatic
    external fun endEventBatch(): Boolean

    @JvmStatic
    external fun drainEvents(): LongArray

    @JvmStatic
    external fun drainNotificationEvents(): Array<ByteArray>

    @JvmStatic
    external fun drainCallEvents(): Array<ByteArray>

    @JvmStatic
    external fun drainMediaEvents(): LongArray

    @JvmStatic
    external fun locationStart(accuracy: Int, timeoutMs: Int): LongArray

    @JvmStatic
    external fun cancelLocation(requestId: Long): Int

    @JvmStatic
    external fun drainLocationEvents(): LongArray

    @JvmStatic
    external fun notificationCommand(command: Int, id: String): Int

    @JvmStatic
    external fun replyMessage(id: String, text: String): Int

    @JvmStatic
    external fun callCommand(command: Int, id: String): Int

    @JvmStatic
    external fun mediaCommand(command: Int): Int

    @JvmStatic
    external fun rfcommCreate(address: String, channel: Int): Long

    @JvmStatic
    external fun rfcommConnect(handle: Long, timeoutMillis: Int): Int

    @JvmStatic
    external fun rfcommRead(handle: Long, buffer: ByteArray, offset: Int, length: Int): Int

    @JvmStatic
    external fun rfcommWrite(handle: Long, buffer: ByteArray, offset: Int, length: Int): Int

    @JvmStatic
    external fun rfcommShutdown(handle: Long)

    @JvmStatic
    external fun rfcommDestroy(handle: Long)
}
