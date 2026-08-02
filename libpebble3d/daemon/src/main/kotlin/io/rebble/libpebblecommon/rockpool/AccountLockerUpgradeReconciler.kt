/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import kotlinx.coroutines.Deferred
import kotlin.coroutines.cancellation.CancellationException

/** Finishes the one-time account-locker ownership upgrade without trusting stale work. */
internal class AccountLockerUpgradeReconciler(
    private val currentToken: () -> String,
    private val markerState: () -> String,
    private val markRetired: () -> Boolean,
    private val markComplete: () -> Boolean,
    private val requestSync: () -> Deferred<Unit>,
) {
    private val logger = Logger.withTag("AccountLockerUpgrade")
    private val lock = Any()
    private var generation = 0L
    private var active: Deferred<Unit>? = null

    fun start() {
        reconcile(currentToken(), mayMarkRetired = false)
    }

    fun accountChanged(change: AccountSessionChange) {
        reconcile(change.token, mayMarkRetired = true)
    }

    /** Retry a retired upgrade after a transient start or synchronization failure. */
    fun retryIfPending() {
        if (markerState() != ACCOUNT_LOCKER_SESSION_GATE_RETIRED) return
        reconcile(currentToken(), mayMarkRetired = false, preserveActive = true)
    }

    /**
     * Recover either half of the upgrade without replaying a retirement which already committed.
     *
     * [retireIfNeeded] must perform the marker write inside the same account transition which
     * retires the old cloud-owned rows.  It is invoked only while no durable retirement marker
     * exists; once that marker is present, periodic recovery only retries the replacement sync.
     */
    suspend fun recoverIfPending(retireIfNeeded: suspend () -> Boolean) {
        when (markerState()) {
            ACCOUNT_LOCKER_SESSION_GATE_COMPLETE -> return
            ACCOUNT_LOCKER_SESSION_GATE_RETIRED -> {
                retryIfPending()
                return
            }
        }

        val retired = try {
            retireIfNeeded()
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logger.w(e) { "could not retry account locker retirement" }
            false
        }
        if (!retired) {
            logger.w { "account locker ownership upgrade is still pending" }
            return
        }

        when (markerState()) {
            ACCOUNT_LOCKER_SESSION_GATE_COMPLETE -> Unit
            ACCOUNT_LOCKER_SESSION_GATE_RETIRED -> retryIfPending()
            else -> logger.w { "account locker retirement completed without a durable marker" }
        }
    }

    private fun reconcile(
        token: String,
        mayMarkRetired: Boolean,
        preserveActive: Boolean = false,
    ) {
        val state = markerState()
        if (state == ACCOUNT_LOCKER_SESSION_GATE_COMPLETE) return
        if (
            state != ACCOUNT_LOCKER_SESSION_GATE_RETIRED &&
            (!mayMarkRetired || !markRetired())
        ) {
            logger.w { "account locker ownership upgrade is still pending" }
            return
        }

        val previous: Deferred<Unit>?
        val sync: Deferred<Unit>?
        val expectedGeneration: Long
        synchronized(lock) {
            // A normal account change deliberately supersedes the previous session.  A periodic
            // retry must instead leave the current synchronization owned through its completion
            // callback. Recheck the durable marker under the same lock: a successful callback
            // may have finalized it after the optimistic state read above.
            if (
                preserveActive &&
                (active != null || markerState() == ACCOUNT_LOCKER_SESSION_GATE_COMPLETE)
            ) {
                return
            }
            generation += 1
            expectedGeneration = generation
            previous = active
            active = null
            sync = if (token.isBlank()) {
                null
            } else {
                runCatching(requestSync)
                    .onFailure {
                        logger.w(it) { "could not start account locker reconciliation" }
                    }
                    .getOrNull()
                    .also { active = it }
            }
        }
        previous?.cancel()

        if (token.isBlank()) {
            finishIfCurrent(expectedGeneration, token, null)
            return
        }
        sync ?: return
        sync.invokeOnCompletion { cause ->
            if (cause == null) {
                finishIfCurrent(expectedGeneration, token, sync)
            } else if (releaseFailedIfCurrent(expectedGeneration, token, sync)) {
                logger.w(cause) { "account locker reconciliation failed" }
            }
        }
    }

    private fun releaseFailedIfCurrent(
        expectedGeneration: Long,
        expectedToken: String,
        sync: Deferred<Unit>,
    ): Boolean = synchronized(lock) {
        if (
            generation != expectedGeneration ||
            currentToken() != expectedToken ||
            active !== sync
        ) {
            false
        } else {
            active = null
            true
        }
    }

    private fun finishIfCurrent(
        expectedGeneration: Long,
        expectedToken: String,
        sync: Deferred<Unit>?,
    ) {
        synchronized(lock) {
            if (
                generation != expectedGeneration ||
                currentToken() != expectedToken ||
                (sync != null && active !== sync)
            ) {
                return
            }
            if (!markComplete()) {
                // The sync succeeded but the durable terminal marker did not. Release ownership
                // so the periodic path can rerun the authoritative sync before retrying the
                // marker, rather than trusting success which was not durably recorded.
                active = null
                logger.w { "account locker upgrade marker could not be finalized" }
                return
            }
            active = null
        }
    }
}
