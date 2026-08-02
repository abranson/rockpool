/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals

class RockworkScanCoordinatorTest {
    @Test
    fun `scan starts only after refreshed addresses are applied and results recomputed`() = runBlocking {
        val enteredRefresh = CompletableDeferred<Unit>()
        val releaseRefresh = CompletableDeferred<Set<String>>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val coordinator = coordinator(scope, events) {
                enteredRefresh.complete(Unit)
                releaseRefresh.await()
            }

            coordinator.start()
            enteredRefresh.await()
            assertEquals(emptyList(), events)

            releaseRefresh.complete(setOf("AA:BB:CC:DD:EE:FF"))
            withTimeout(1_000) {
                while (events.size < 3) kotlinx.coroutines.yield()
            }
            assertEquals(listOf("apply:AA:BB:CC:DD:EE:FF", "recompute", "start"), events)
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `stop prevents a slow refresh from changing state or restarting scan`() = runBlocking {
        val enteredRefresh = CompletableDeferred<Unit>()
        val releaseRefresh = CompletableDeferred<Set<String>>()
        val stopped = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val coordinator = coordinator(scope, events, stop = { stopped.complete(Unit) }) {
                enteredRefresh.complete(Unit)
                releaseRefresh.await()
            }

            coordinator.start()
            enteredRefresh.await()
            coordinator.stop()
            stopped.await()
            releaseRefresh.complete(setOf("AA:BB:CC:DD:EE:FF"))
            withTimeout(1_000) { kotlinx.coroutines.yield() }

            assertEquals(listOf("stop"), events)
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `newer scan owns state when an older refresh finishes last`() = runBlocking {
        val firstEntered = CompletableDeferred<Unit>()
        val releaseFirst = CompletableDeferred<Set<String>>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            var refreshes = 0
            val coordinator = coordinator(scope, events) {
                if (++refreshes == 1) {
                    firstEntered.complete(Unit)
                    releaseFirst.await()
                } else {
                    setOf("11:22:33:44:55:66")
                }
            }

            coordinator.start()
            firstEntered.await()
            coordinator.start()
            withTimeout(1_000) {
                while (events.size < 3) kotlinx.coroutines.yield()
            }
            releaseFirst.complete(setOf("AA:BB:CC:DD:EE:FF"))
            withTimeout(1_000) { kotlinx.coroutines.yield() }

            assertEquals(listOf("apply:11:22:33:44:55:66", "recompute", "start"), events)
        } finally {
            scope.cancel()
        }
    }

    private fun coordinator(
        scope: CoroutineScope,
        events: MutableList<String>,
        stop: () -> Unit = {},
        refresh: suspend () -> Set<String>,
    ) = RockworkScanCoordinator(
        scope = scope,
        refreshBondedAddresses = refresh,
        applyBondedAddresses = { events += "apply:${it.single()}" },
        recomputeResults = { events += "recompute" },
        startScan = { events += "start" },
        stopScan = { events += "stop"; stop() },
        onRefreshFailure = { throw AssertionError("unexpected refresh failure", it) },
        onScanFailure = { operation, cause ->
            throw AssertionError("unexpected $operation failure", cause)
        },
        dispatcher = Dispatchers.Unconfined,
    )
}
