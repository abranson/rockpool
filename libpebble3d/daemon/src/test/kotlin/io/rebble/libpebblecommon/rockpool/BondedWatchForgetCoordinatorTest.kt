/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class BondedWatchForgetCoordinatorTest {
    private val classic = "11:22:33:44:55:66"
    private val le = "02:11:22:33:12:34"

    @Test
    fun `removes every alias before retiring portable state`() = runBlocking {
        val events = mutableListOf<String>()
        val coordinator = BondedWatchForgetCoordinator(
            prepareRemoval = { address, name ->
                events += "prepare:$address:$name"
                PreparedBondRemoval(listOf(classic, le)) {
                    events += "remove"
                    true
                }
            },
            settleConnection = { events += "settle" },
        )

        val outcome = coordinator.forget(
            address = classic.lowercase(),
            name = "Pebble Time 1234",
            beginCommit = {
                events += "commit"
                true
            },
            disconnect = { events += "disconnect" },
            forgetPortable = { events += "forget" },
        )

        assertEquals(BondedWatchForgetOutcome.COMPLETED, outcome)
        assertEquals(
            listOf(
                "prepare:${classic.lowercase()}:Pebble Time 1234",
                "commit",
                "disconnect",
                "settle",
                "remove",
                "forget",
            ),
            events,
        )
    }

    @Test
    fun `reconnect through either alias is rejected after forget commits`() = runBlocking {
        val settling = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        var removed = false
        var forgotten = false
        val coordinator = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ ->
                PreparedBondRemoval(listOf(classic, le)) {
                    removed = true
                    true
                }
            },
            settleConnection = {
                settling.complete(Unit)
                release.await()
            },
        )
        val forget = async(Dispatchers.Unconfined) {
            coordinator.forget(
                address = classic,
                name = "Pebble Time 1234",
                beginCommit = { true },
                disconnect = {},
                forgetPortable = { forgotten = true },
            )
        }
        settling.await()

        assertFalse(coordinator.connect(le.lowercase()))
        release.complete(Unit)

        assertEquals(BondedWatchForgetOutcome.COMPLETED, forget.await())
        assertTrue(removed)
        assertTrue(forgotten)
    }

    @Test
    fun `reconnect during alias discovery supersedes forget before disconnect`() = runBlocking {
        val preparing = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        var disconnected = false
        var removed = false
        val coordinator = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ ->
                preparing.complete(Unit)
                release.await()
                PreparedBondRemoval(listOf(classic, le)) {
                    removed = true
                    true
                }
            },
            settleConnection = {},
        )
        val forget = async(Dispatchers.Unconfined) {
            coordinator.forget(
                address = classic,
                name = "Pebble Time 1234",
                beginCommit = { true },
                disconnect = { disconnected = true },
                forgetPortable = {},
            )
        }
        preparing.await()

        assertTrue(coordinator.connect(le.lowercase()))
        release.complete(Unit)

        assertEquals(BondedWatchForgetOutcome.SUPERSEDED, forget.await())
        assertFalse(disconnected)
        assertFalse(removed)
    }

    @Test
    fun `unrelated reconnect leaves a pending forget current`() = runBlocking {
        val settling = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val coordinator = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ -> PreparedBondRemoval(listOf(classic, le)) { true } },
            settleConnection = {
                settling.complete(Unit)
                release.await()
            },
        )
        val forget = async(Dispatchers.Unconfined) {
            coordinator.forget(classic, "Pebble Time 1234", { true }, {}, {})
        }
        settling.await()

        assertTrue(coordinator.connect("AA:BB:CC:DD:EE:FF"))
        release.complete(Unit)

        assertEquals(BondedWatchForgetOutcome.COMPLETED, forget.await())
    }

    @Test
    fun `committing forget rejects reconnect until portable state is retired`() = runBlocking {
        val removing = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        var forgotten = false
        val coordinator = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ ->
                PreparedBondRemoval(listOf(classic, le)) {
                    removing.complete(Unit)
                    release.await()
                    true
                }
            },
            settleConnection = {},
        )
        val forget = async(Dispatchers.Unconfined) {
            coordinator.forget(
                classic,
                "Pebble Time 1234",
                { true },
                {},
                { forgotten = true },
            )
        }
        removing.await()

        assertFalse(coordinator.connect(le))
        release.complete(Unit)

        assertEquals(BondedWatchForgetOutcome.COMPLETED, forget.await())
        assertTrue(forgotten)
        assertTrue(coordinator.connect(le))
    }

    @Test
    fun `committing forget rejects a second forget through another alias`() = runBlocking {
        val removing = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        var preparations = 0
        var secondForgotten = false
        val coordinator = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ ->
                preparations += 1
                PreparedBondRemoval(listOf(classic, le)) {
                    removing.complete(Unit)
                    release.await()
                    true
                }
            },
            settleConnection = {},
        )
        val first = async(Dispatchers.Unconfined) {
            coordinator.forget(classic, "Pebble Time 1234", { true }, {}, {})
        }
        removing.await()

        assertEquals(
            BondedWatchForgetOutcome.BUSY,
            coordinator.forget(
                le,
                "Pebble Time Le 1234",
                { true },
                {},
                { secondForgotten = true },
            ),
        )
        assertEquals(1, preparations)
        assertFalse(secondForgotten)

        release.complete(Unit)
        assertEquals(BondedWatchForgetOutcome.COMPLETED, first.await())
    }

    @Test
    fun `unavailable cancelled and failed cleanup preserve portable state`() = runBlocking {
        var forgotten = false
        var removed = false
        var disconnected = false
        var settled = false
        val unavailable = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ -> null },
            settleConnection = {},
        )
        assertEquals(
            BondedWatchForgetOutcome.UNAVAILABLE,
            unavailable.forget(classic, "Pebble Time 1234", { true }, {}, { forgotten = true }),
        )

        val cancelled = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ ->
                PreparedBondRemoval(listOf(classic)) {
                    removed = true
                    true
                }
            },
            settleConnection = { settled = true },
        )
        assertEquals(
            BondedWatchForgetOutcome.CANCELLED,
            cancelled.forget(
                classic,
                "Pebble Time 1234",
                { false },
                { disconnected = true },
                { forgotten = true },
            ),
        )

        val failed = BondedWatchForgetCoordinator(
            prepareRemoval = { _, _ -> PreparedBondRemoval(listOf(classic, le)) { false } },
            settleConnection = {},
        )
        assertEquals(
            BondedWatchForgetOutcome.REMOVAL_FAILED,
            failed.forget(classic, "Pebble Time 1234", { true }, {}, { forgotten = true }),
        )
        assertFalse(disconnected)
        assertFalse(settled)
        assertFalse(removed)
        assertFalse(forgotten)
    }
}
