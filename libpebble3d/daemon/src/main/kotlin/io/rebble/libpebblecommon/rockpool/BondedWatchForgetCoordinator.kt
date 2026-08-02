/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.bt.ble.bluez.BluezManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.time.Duration.Companion.seconds

internal class PreparedBondRemoval(
    aliases: Collection<String>,
    val remove: suspend () -> Boolean,
) {
    val aliases: Set<String> = aliases.mapTo(linkedSetOf(), ::normalizeBluetoothAddress)

    init {
        require(this.aliases.isNotEmpty())
    }
}

internal enum class BondedWatchForgetOutcome {
    COMPLETED,
    UNAVAILABLE,
    BUSY,
    SUPERSEDED,
    CANCELLED,
    REMOVAL_FAILED,
}

/**
 * Linearizes one logical watch's BlueZ aliases, reconnects, and portable-state retirement.
 *
 * BlueZ is authoritative for the alias set, so it must be snapshotted while the portable record
 * still exists.  Once [beginCommit] wins, bond removal and [forgetPortable] are non-cancellable;
 * a later connect is rejected until both sides have reached the same forgotten state.
 */
internal class BondedWatchForgetCoordinator(
    private val prepareRemoval: suspend (address: String, name: String) -> PreparedBondRemoval?,
    private val settleConnection: suspend () -> Unit = { delay(2.seconds) },
) {
    private enum class State {
        PREPARING,
        PENDING,
        COMMITTING,
    }

    private class Attempt(
        val sequence: Long,
        initialAlias: String,
    ) {
        var aliases: Set<String> = setOf(initialAlias)
        val connectionsWhilePreparing = linkedSetOf<String>()
        var state: State = State.PREPARING
    }

    private val lock = Any()
    private val attempts = linkedSetOf<Attempt>()
    private var nextSequence = 0L

    /**
     * Start a connection lifecycle. Returns false only when an irreversible Forget commit already
     * owns this alias; otherwise any pending Forget for the logical watch is superseded.
     */
    fun connect(address: String): Boolean = synchronized(lock) {
        val normalized = normalizeBluetoothAddress(address)
        if (attempts.any { normalized in it.aliases && it.state == State.COMMITTING }) {
            return@synchronized false
        }
        attempts
            .filter { it.state == State.PREPARING }
            .forEach { it.connectionsWhilePreparing += normalized }
        attempts.removeAll { normalized in it.aliases && it.state == State.PENDING }
        true
    }

    suspend fun forget(
        address: String,
        name: String,
        beginCommit: () -> Boolean,
        disconnect: () -> Unit,
        forgetPortable: () -> Unit,
    ): BondedWatchForgetOutcome {
        val initialAlias = normalizeBluetoothAddress(address)
        val attempt = synchronized(lock) {
            if (attempts.any { initialAlias in it.aliases && it.state == State.COMMITTING }) {
                null
            } else {
                val created = Attempt(++nextSequence, initialAlias)
                attempts.removeAll { existing ->
                    initialAlias in existing.aliases && existing.state != State.COMMITTING
                }
                attempts += created
                created
            }
        } ?: return BondedWatchForgetOutcome.BUSY

        try {
            val prepared = prepareRemoval(address, name)
                ?: return synchronized(lock) {
                    val current = attempt in attempts
                    attempts.remove(attempt)
                    if (current && initialAlias !in attempt.connectionsWhilePreparing) {
                        BondedWatchForgetOutcome.UNAVAILABLE
                    } else {
                        BondedWatchForgetOutcome.SUPERSEDED
                    }
                }
            val preparedAliases = prepared.aliases + initialAlias
            val preparedOutcome = synchronized(lock) {
                if (attempt !in attempts) {
                    BondedWatchForgetOutcome.SUPERSEDED
                } else if (attempt.connectionsWhilePreparing.any(preparedAliases::contains)) {
                    attempts.remove(attempt)
                    BondedWatchForgetOutcome.SUPERSEDED
                } else {
                    val overlapping = attempts.filter { existing ->
                        existing !== attempt && existing.aliases.any(preparedAliases::contains)
                    }
                    when {
                        overlapping.any { it.state == State.COMMITTING } -> {
                            attempts.remove(attempt)
                            BondedWatchForgetOutcome.BUSY
                        }
                        overlapping.any { it.sequence > attempt.sequence } -> {
                            attempts.remove(attempt)
                            BondedWatchForgetOutcome.SUPERSEDED
                        }
                        else -> {
                            attempts.removeAll { it in overlapping }
                            attempt.aliases = preparedAliases
                            attempt.state = State.PENDING
                            null
                        }
                    }
                }
            }
            if (preparedOutcome != null) return preparedOutcome

            val claim = synchronized(lock) {
                if (attempt !in attempts || attempt.state != State.PENDING) {
                    BondedWatchForgetOutcome.SUPERSEDED
                } else if (!beginCommit()) {
                    attempts.remove(attempt)
                    BondedWatchForgetOutcome.CANCELLED
                } else {
                    attempt.state = State.COMMITTING
                    null
                }
            }
            if (claim != null) return claim

            return withContext(NonCancellable) {
                disconnect()
                settleConnection()
                if (!prepared.remove()) {
                    BondedWatchForgetOutcome.REMOVAL_FAILED
                } else {
                    forgetPortable()
                    BondedWatchForgetOutcome.COMPLETED
                }
            }
        } finally {
            synchronized(lock) { attempts.remove(attempt) }
        }
    }
}

internal fun createBluezBondedWatchForgetCoordinator(): BondedWatchForgetCoordinator =
    BondedWatchForgetCoordinator(
        prepareRemoval = { address, name ->
            withContext(Dispatchers.IO) {
                BluezManager.prepareBondRemoval(address, name)?.let { plan ->
                    PreparedBondRemoval(plan.addresses) {
                        withContext(Dispatchers.IO) { plan.removeAll() }
                    }
                }
            }
        },
    )

private fun normalizeBluetoothAddress(address: String): String = address.uppercase()
