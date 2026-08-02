/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.health.HealthSettings
import io.rebble.libpebblecommon.health.HealthSettingsInitialization
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.retryWhen
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds

/** The serialized result of an account-global health-settings write. */
internal data class HealthSettingsUpdate(
    val saved: Boolean,
    val previous: HealthSettings,
    val current: HealthSettings,
)

internal enum class HealthSettingsInitializationResult {
    Initialized,
    Existing,
}

/**
 * Serializes health read/merge/write operations across both D-Bus services.
 *
 * libpebble3 stores one account-global [HealthSettings] record. Daemon writes use the concrete
 * transactional persistence seam while this mutex is held, so primary and compatibility updates
 * cannot overlap or report success before all four durable rows have committed.
 */
internal class HealthSettingsCoordinator(
    private val libPebble: LibPebble,
    private val healthWriteTimeout: Duration = 5.seconds,
    private val persistHealthSettings: suspend (HealthSettings) -> Unit,
) {
    private val logger = Logger.withTag("HealthSettings")
    private val mutex = Mutex()
    private val listenerLock = Any()
    private val listeners = mutableListOf<(HealthSettingsUpdate) -> Unit>()
    private val observationStarted = AtomicBoolean(false)
    private var lastObserved: HealthSettings? = null
    private var lastDelivered: HealthSettings? = null

    fun addListener(listener: (HealthSettingsUpdate) -> Unit) {
        synchronized(listenerLock) { listeners += listener }
    }

    /** Observe watch/Room-origin settings changes and fan them out through the same listeners. */
    fun startObserving(scope: CoroutineScope) {
        if (!observationStarted.compareAndSet(false, true)) return
        scope.launch {
            libPebble.healthSettings
                .retryWhen { cause, _ ->
                    logger.w(cause) { "health settings observation failed; retrying" }
                    delay(HEALTH_OBSERVATION_RETRY)
                    true
                }
                .collect(::observed)
        }
    }

    fun current(): HealthSettings = runBlocking { currentSuspend() }

    suspend fun currentSuspend(): HealthSettings = mutex.withLock { awaitCurrent() }

    fun update(
        transform: (HealthSettings) -> HealthSettings,
        commit: () -> Boolean,
    ): Boolean = updateWithResult(transform, commit).saved

    fun updateWithResult(
        transform: (HealthSettings) -> HealthSettings,
        commit: () -> Boolean,
    ): HealthSettingsUpdate = runBlocking {
        updateWithResultSuspend(transform, commit)
    }

    suspend fun updateWithResultSuspend(
        transform: (HealthSettings) -> HealthSettings,
        commit: () -> Boolean,
    ): HealthSettingsUpdate {
        val result = mutex.withLock {
            val current = awaitCurrent()
            val updated = transform(current)
            if (!commit()) {
                return@withLock HealthSettingsUpdate(
                    saved = false,
                    previous = current,
                    current = current,
                )
            }
            val persisted = withTimeoutOrNull(healthWriteTimeout) {
                persistHealthSettings(updated)
                true
            }
            check(persisted == true) { "health settings persistence timed out" }
            HealthSettingsUpdate(saved = true, previous = current, current = updated)
        }

        if (result.saved) publish(result)
        return result
    }

    /**
     * Initialize a fresh libpebble3 health store without racing a durable D-Bus write.
     * [initialize] performs its raw-database absent check and complete insertion atomically.
     */
    suspend fun initializeIfAbsent(
        transform: (HealthSettings) -> HealthSettings,
        initialize: suspend (
            transform: (HealthSettings) -> HealthSettings,
        ) -> HealthSettingsInitialization?,
    ): HealthSettingsInitializationResult {
        var applied: HealthSettingsUpdate? = null
        val result = mutex.withLock {
            val initialized = initialize(transform)
            if (initialized == null) {
                return@withLock HealthSettingsInitializationResult.Existing
            }
            applied = HealthSettingsUpdate(
                saved = true,
                previous = initialized.previous,
                current = initialized.current,
            )
            HealthSettingsInitializationResult.Initialized
        }

        applied?.let(::publish)
        return result
    }

    private suspend fun awaitCurrent(): HealthSettings = withTimeout(HEALTH_READ_TIMEOUT) {
        libPebble.healthSettings.first()
    }

    private fun observed(current: HealthSettings) {
        val update = synchronized(listenerLock) {
            val previous = lastObserved
            lastObserved = current
            previous?.takeIf { it != current }?.let {
                HealthSettingsUpdate(saved = true, previous = it, current = current)
            }
        }
        update?.let(::publish)
    }

    private fun publish(update: HealthSettingsUpdate) {
        if (update.previous == update.current) return
        val targets = synchronized(listenerLock) {
            if (lastDelivered == update.current) return
            lastDelivered = update.current
            listeners.toList()
        }
        targets.forEach { listener ->
            runCatching { listener(update) }
                .onFailure { logger.w(it) { "health settings listener failed" } }
        }
    }

    private companion object {
        val HEALTH_READ_TIMEOUT = 5.seconds
        val HEALTH_OBSERVATION_RETRY = 1.seconds
    }
}
