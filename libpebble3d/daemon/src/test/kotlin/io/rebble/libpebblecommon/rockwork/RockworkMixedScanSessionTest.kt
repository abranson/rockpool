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
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RockworkMixedScanSessionTest {
    @Test
    fun `one legacy scan runs BLE then Classic and stops both`() = runBlocking {
        val first = CompletableDeferred<Unit>()
        val second = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            var phase = 0
            val session = session(scope, events) {
                if (phase++ == 0) first.await() else second.await()
            }

            session.start()
            assertTrue(session.isActive())
            assertEquals(listOf("started", "active:true", "stop-ble", "stop-classic", "start-ble"), events)

            first.complete(Unit)
            withTimeout(1_000) {
                while ("start-classic" !in events) kotlinx.coroutines.yield()
            }
            second.complete(Unit)
            withTimeout(1_000) {
                while (session.isActive()) kotlinx.coroutines.yield()
            }

            assertEquals(
                listOf(
                    "started", "active:true", "stop-ble", "stop-classic", "start-ble",
                    "stop-ble", "start-classic", "stop-ble", "stop-classic", "active:false",
                ),
                events,
            )
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `stop retires a delayed phase so it cannot start Classic`() = runBlocking {
        val release = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val session = session(scope, events) { release.await() }
            session.start()
            session.stop()
            release.complete(Unit)
            kotlinx.coroutines.yield()

            assertFalse(session.isActive())
            assertEquals(0, events.count { it == "start-classic" })
            assertEquals(1, events.count { it == "active:false" })
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `unavailable Classic keeps BLE active for the full legacy window`() = runBlocking {
        val first = CompletableDeferred<Unit>()
        val second = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            var phase = 0
            val session = session(scope, events, classicAvailable = false) {
                if (phase++ == 0) first.await() else second.await()
            }
            session.start()
            first.complete(Unit)
            kotlinx.coroutines.yield()

            assertTrue(session.isActive())
            assertEquals(0, events.count { it == "start-classic" })
            assertEquals(1, events.count { it == "start-ble" })

            second.complete(Unit)
            withTimeout(1_000) {
                while (session.isActive()) kotlinx.coroutines.yield()
            }
        } finally {
            scope.cancel()
        }
    }

    private fun session(
        scope: CoroutineScope,
        events: MutableList<String>,
        classicAvailable: Boolean = true,
        waitForPhase: suspend () -> Unit,
    ) = RockworkMixedScanSession(
        scope = scope,
        startBle = { events += "start-ble" },
        stopBle = { events += "stop-ble" },
        startClassic = { events += "start-classic" },
        stopClassic = { events += "stop-classic" },
        classicAvailable = classicAvailable,
        onStarted = { events += "started" },
        onActiveChanged = { events += "active:$it" },
        onFailure = { name, cause -> throw AssertionError("unexpected $name failure", cause) },
        waitForPhase = waitForPhase,
    )
}
