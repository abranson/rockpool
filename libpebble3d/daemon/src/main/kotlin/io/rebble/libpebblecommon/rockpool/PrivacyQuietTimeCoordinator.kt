/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.WatchPrefs
import io.rebble.libpebblecommon.ui.quietTimeSnapshot
import io.rebble.libpebblecommon.ui.saveQuietTime
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.merge
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlin.time.Duration.Companion.seconds

/** A durable manual Quiet Time override; schedules remain owned by the watch library. */
internal class PrivacyQuietTimeCoordinator(
    private val settings: RockpoolSettings,
    private val prefs: WatchPrefs,
    private val privacyActive: StateFlow<Boolean?>,
    private val syncEnabled: () -> Boolean,
) {
    private val logger = Logger.withTag("PrivacyQuietTime")
    private val mutex = Mutex()
    private var pendingManualWrite: String? = null
    private val refresh = Channel<Unit>(Channel.CONFLATED)
    private val mutableRevision = MutableStateFlow(0L)
    val revision: StateFlow<Long> = mutableRevision.asStateFlow()
    val supported: Boolean get() = privacyActive.value != null
    val enabled: Boolean get() = settings.getBool(ENABLED)
    val overriding: Boolean get() = previous() != null

    fun start(scope: CoroutineScope, connectionAndConfigChanges: Flow<Unit>) {
        scope.launch {
            merge(
                privacyActive.map { changed(); Unit },
                prefs.watchPrefs.map { Unit },
                connectionAndConfigChanges,
            ).collect { refresh.trySend(Unit) }
        }
        scope.launch {
            for (ignored in refresh) reconcile()
        }
        scope.launch {
            combine(privacyActive, revision) { active, _ -> overriding || (enabled && active == true) }
                .distinctUntilChanged().collectLatest { needsRetry ->
                    if (needsRetry) while (true) {
                        delay(30.seconds)
                        refresh.trySend(Unit)
                    }
                }
        }
    }

    suspend fun setEnabled(value: Boolean): Boolean = mutex.withLock {
        if (value && !supported) return@withLock false
        if (!settings.setChecked(ENABLED, value.toString())) return@withLock false
        changed()
        refresh.trySend(Unit)
        true
    }

    suspend fun setManual(value: String): Boolean = mutex.withLock {
        // A phone-side manual edit must not race the saved-state transaction.
        if (overriding || (enabled && privacyActive.value == true) || !syncEnabled()) {
            return@withLock false
        }
        saveQuietTime(prefs, MANUAL, value)
    }

    suspend fun reconcile(): Boolean = mutex.withLock {
        try {
            val active = privacyActive.value
            // Loss of the optional service is not a switch-off event.
            if (enabled && active == null) return@withLock true
            val applyOverride = enabled && active == true
            var saved = previous()
            if (applyOverride && !syncEnabled()) return@withLock true
            if (!applyOverride && saved == null) return@withLock true

            val current = withTimeout(5.seconds) { quietTimeSnapshot(prefs.watchPrefs.first())[MANUAL] }
                ?: return@withLock false
            // The library setter launches its own coroutine. A timeout does not cancel that
            // write: wait for it before restoring, even if the old value still looks restored.
            pendingManualWrite?.let { pending ->
                if (current != pending) return@withLock false
                pendingManualWrite = null
            }
            if (applyOverride && saved == null) {
                // Commit the original value before modifying the library database, including
                // while disconnected. Never replace it with our own value on reconnect/restart.
                if (!settings.setChecked(PREVIOUS, current)) return@withLock false
                saved = current
                changed()
            }
            val desired = if (applyOverride) "1" else requireNotNull(saved)
            if (current != desired) {
                try {
                    if (!saveQuietTime(prefs, MANUAL, desired)) return@withLock false
                } catch (e: TimeoutCancellationException) {
                    pendingManualWrite = desired
                    throw e
                }
            }
            if (!applyOverride) {
                // Library persistence is sufficient here: its normal BlobDB sync delivers the
                // restored preference on reconnect, or when watch settings sync is re-enabled.
                if (!settings.replacePrefix(PREVIOUS, emptyMap())) return@withLock false
                changed()
            }
            true
        } catch (_: TimeoutCancellationException) {
            logger.w { "Quiet Time persistence timed out; retaining the privacy restore state" }
            false
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logger.w { "Quiet Time privacy update failed: ${e.message}" }
            false
        }
    }

    private fun previous(): String? = settings.get(PREVIOUS).takeIf { it == "0" || it == "1" }

    private fun changed() {
        mutableRevision.value += 1
    }

    private companion object {
        const val ENABLED = "quietTime.followPrivacySwitch"
        const val PREVIOUS = "quietTime.privacyPreviousManual"
        const val MANUAL = "dndManuallyEnabled"
    }
}
