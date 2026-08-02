/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Deferred

internal sealed interface AccountSyncResult {
    data object Completed : AccountSyncResult
    data object Busy : AccountSyncResult
    data object AccountChanged : AccountSyncResult
    data object Cancelled : AccountSyncResult
    data class Failed(val cause: Exception) : AccountSyncResult
}

/**
 * Owns the one account-global locker synchronization slot.
 *
 * A slot is reserved before [start] is called because starting libpebble3's
 * sync is itself synchronous and may throw. The operation crosses its commit
 * boundary immediately before that start, because it may synchronously persist
 * locker data. Every exit path retires exactly that reservation, so a failed
 * start cannot leave later Sync calls Busy.
 */
internal class AccountSyncCoordinator(
    private val stateChanged: (String) -> Unit = {},
) {
    private val lock = Any()
    private var accountGeneration = 0L
    private var activeGeneration: Long? = null
    private var activeSync: Deferred<Unit>? = null
    private var syncState = "idle"

    fun state(): String = synchronized(lock) { syncState }

    fun accountChanged(generation: Long) {
        val obsolete = synchronized(lock) {
            accountGeneration = generation
            activeGeneration = null
            syncState = "idle"
            activeSync.also { activeSync = null }
        }
        obsolete?.cancel()
    }

    suspend fun synchronize(
        beginCommit: () -> Boolean,
        start: () -> Deferred<Unit>,
    ): AccountSyncResult {
        val generation = synchronized(lock) {
            if (activeGeneration != null) return AccountSyncResult.Busy
            accountGeneration.also { activeGeneration = it }
        }
        var sync: Deferred<Unit>? = null
        try {
            if (!beginCommit()) return AccountSyncResult.Cancelled

            sync = try {
                start()
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                return if (setStateIfCurrent(generation, null, "failed")) {
                    AccountSyncResult.Failed(e)
                } else {
                    AccountSyncResult.AccountChanged
                }
            }

            val accepted = synchronized(lock) {
                if (
                    generation != accountGeneration ||
                    activeGeneration != generation
                ) {
                    false
                } else {
                    activeSync = sync
                    syncState = "syncing"
                    true
                }
            }
            if (!accepted) {
                sync.cancel()
                return AccountSyncResult.AccountChanged
            }
            stateChanged("syncing")

            try {
                sync.await()
            } catch (e: CancellationException) {
                sync.cancel()
                if (!setStateIfCurrent(generation, sync, "idle")) {
                    return AccountSyncResult.AccountChanged
                }
                throw e
            } catch (e: Exception) {
                return if (setStateIfCurrent(generation, sync, "failed")) {
                    AccountSyncResult.Failed(e)
                } else {
                    AccountSyncResult.AccountChanged
                }
            }

            val result = synchronized(lock) {
                if (
                    generation != accountGeneration ||
                    activeGeneration != generation ||
                    activeSync !== sync
                ) {
                    AccountSyncResult.AccountChanged
                } else {
                    syncState = "idle"
                    AccountSyncResult.Completed
                }
            }
            if (result === AccountSyncResult.Completed) {
                stateChanged("idle")
            }
            return result
        } finally {
            synchronized(lock) {
                if (
                    activeGeneration == generation &&
                    (sync == null || activeSync === sync)
                ) {
                    activeSync = null
                    activeGeneration = null
                }
            }
        }
    }

    private fun setStateIfCurrent(
        generation: Long,
        sync: Deferred<Unit>?,
        state: String,
    ): Boolean {
        val changed = synchronized(lock) {
            if (
                generation != accountGeneration ||
                activeGeneration != generation ||
                (sync != null && activeSync !== sync)
            ) {
                false
            } else {
                syncState = state
                true
            }
        }
        if (changed) stateChanged(state)
        return changed
    }
}
