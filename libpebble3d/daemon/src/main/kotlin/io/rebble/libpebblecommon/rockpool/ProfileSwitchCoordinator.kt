/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.CommonConnectedDevice
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.linux.dbus.RawSessionConnection
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.runInterruptible
import kotlin.coroutines.cancellation.CancellationException
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.SynchronousQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import java.util.concurrent.atomic.AtomicBoolean

internal fun interface ProfileSetter {
    suspend fun setProfile(profile: String)
}

internal data class ProfileWatchState(
    val address: String,
    val connected: Boolean,
)

/** Applies the old "profile when connected/disconnected" settings independently of D-Bus APIs. */
internal class ProfileSwitchCoordinator(
    private val watches: Flow<List<ProfileWatchState>>,
    private val getSetting: (String) -> String,
    private val profileSetter: ProfileSetter,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
) {
    constructor(libPebble: LibPebble, settings: RockpoolSettings) : this(
        watches = libPebble.watches.map { devices ->
            devices.filterIsInstance<KnownPebbleDevice>().map { device ->
                ProfileWatchState(
                    address = device.identifier.asString.uppercase(),
                    connected = device is CommonConnectedDevice,
                )
            }
        },
        getSetting = settings::get,
        profileSetter = ProfileSetter { profile -> setSailfishProfile(profile) },
    )

    private val connected = mutableMapOf<String, Boolean>()
    private val pendingLock = Any()
    private val pendingTransitions = linkedMapOf<String, ProfileWatchState>()
    private val transitionReady = Channel<Unit>(Channel.CONFLATED)
    private val started = AtomicBoolean(false)

    fun start() {
        if (!started.compareAndSet(false, true)) return
        scope.launch {
            for (ignored in transitionReady) {
                while (true) {
                    val transition = takePendingTransition() ?: break
                    applyProfile(transition.address, transition.connected)
                }
            }
        }
        scope.launch {
            watches.collect { states ->
                val current = states.associateBy { it.address }
                connected.keys.retainAll(current.keys)
                synchronized(pendingLock) {
                    pendingTransitions.keys.retainAll(current.keys)
                }
                current.forEach { (address, state) ->
                    val previous = connected.put(address, state.connected)
                    if (previous != null && previous != state.connected) {
                        queueTransition(state)
                    }
                }
            }
        }
    }

    private fun queueTransition(state: ProfileWatchState) {
        synchronized(pendingLock) {
            // Only the latest desired profile for each watch is useful. In particular, do not
            // build an unbounded replay queue while the system profile service is unavailable.
            pendingTransitions[state.address] = state
        }
        transitionReady.trySend(Unit)
    }

    private fun takePendingTransition(): ProfileWatchState? = synchronized(pendingLock) {
        val address = pendingTransitions.keys.firstOrNull() ?: return@synchronized null
        pendingTransitions.remove(address)
    }

    private suspend fun applyProfile(address: String, isConnected: Boolean) {
        val kind = if (isConnected) "connected" else "disconnected"
        val profile = getSetting("${address.replace(":", "_")}.profile.$kind")
        if (profile.isEmpty()) return
        try {
            profileSetter.setProfile(profile)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logger.w { "set_profile failed: ${e.message}" }
        }
    }

    private companion object {
        private val logger = Logger.withTag("ProfileSwitchCoordinator")
        private const val PROFILE_DBUS_TIMEOUT_SECONDS = 3L
        private val profileExecutor = ThreadPoolExecutor(
            1,
            1,
            0L,
            TimeUnit.MILLISECONDS,
            SynchronousQueue(),
            { task -> Thread(task, "rockpool-profile-setter").apply { isDaemon = true } },
            ThreadPoolExecutor.AbortPolicy(),
        )

        private suspend fun setSailfishProfile(profile: String) = runInterruptible(Dispatchers.IO) {
            val request = try {
                profileExecutor.submit<Boolean> {
                    val connection = RawSessionConnection.connect() ?: return@submit false
                    connection.use { conn ->
                        conn.call(
                            "com.nokia.profiled", "/com/nokia/profiled", "com.nokia.profiled",
                            "set_profile", "s", profile,
                        )
                    }
                    true
                }
            } catch (_: RejectedExecutionException) {
                throw IllegalStateException("previous set_profile request is still blocked")
            }
            try {
                if (!request.get(PROFILE_DBUS_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                    throw IllegalStateException("could not connect to session bus for set_profile")
                }
            } catch (e: TimeoutException) {
                request.cancel(true)
                throw IllegalStateException("set_profile timed out", e)
            } catch (e: InterruptedException) {
                request.cancel(true)
                Thread.currentThread().interrupt()
                throw CancellationException("set_profile was interrupted").apply { initCause(e) }
            }
        }
    }
}
