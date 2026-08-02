/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class PrimaryConnectionAttemptRegistryTest {
    @Test
    fun `successful pair cannot be claimed by later cancellation`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, PAIR) {})

        assertTrue(registry.complete(attempt))
        assertTrue(registry.claimAllCleanup().isEmpty())
        assertFalse(registry.claimCleanup(attempt))
    }

    @Test
    fun `pair and watch connect are mutually exclusive case insensitively`() {
        val registry = PrimaryConnectionAttemptRegistry()

        assertNotNull(registry.begin(ADDRESS.lowercase(), PAIR) {})
        assertNull(registry.begin(ADDRESS, WATCH_CONNECT) {})
    }

    @Test
    fun `abandoned pending attempt cannot request a connection`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        var requests = 0

        assertTrue(registry.abandonPending(attempt))
        assertFalse(registry.requestConnectIfPending(attempt) { requests++ })
        assertEquals(0, requests)
    }

    @Test
    fun `cancel pairing wins before queued pair connect request`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        var requests = 0
        var cancellations = 0

        assertEquals(listOf(attempt), registry.claimAllCleanup(PAIR))
        assertFalse(registry.requestConnectIfPending(attempt) { requests++ })
        assertTrue(registry.retireConnectionIfCleaning(attempt) { cancellations++ })

        assertEquals(0, requests)
        assertEquals(0, cancellations)
        assertEquals(PrimaryConnectionCleanupState.RETIRING, registry.cleanupState(attempt))
    }

    @Test
    fun `started connection is cancelled and retained while retiring`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        var requests = 0
        var cancels = 0

        assertTrue(registry.requestConnectIfPending(attempt) { requests++ })
        assertTrue(registry.claimCleanup(attempt))
        assertTrue(registry.retireConnectionIfCleaning(attempt) { cancels++ })

        assertEquals(1, requests)
        assertEquals(1, cancels)
        assertEquals(PrimaryConnectionCleanupState.RETIRING, registry.cleanupState(attempt))
        assertNull(registry.begin(ADDRESS.lowercase(), WATCH_CONNECT) {})
        registry.finishCleanup(attempt)
        assertNotNull(registry.begin(ADDRESS.lowercase(), WATCH_CONNECT) {})
    }

    @Test
    fun `manager cancellation still cancels a pair after pair commit`() {
        val registry = PrimaryConnectionAttemptRegistry()
        var operationCancellations = 0
        var goalCancellations = 0
        val attempt = assertNotNull(
            registry.begin(ADDRESS, PAIR) { operationCancellations++ },
        )

        assertTrue(registry.requestConnectIfPending(attempt) {})
        assertEquals(listOf(attempt), registry.claimAllCleanup(PAIR))
        attempt.cancelOperation()
        assertTrue(registry.retireConnectionIfCleaning(attempt) { goalCancellations++ })

        assertEquals(1, operationCancellations)
        assertEquals(1, goalCancellations)
    }

    @Test
    fun `pair and watch connect share the request and retirement lifecycle`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val pair = assertNotNull(registry.begin(ADDRESS, PAIR) {})

        assertTrue(registry.requestConnectIfPending(pair) {})
        assertNull(registry.begin(ADDRESS.lowercase(), WATCH_CONNECT) {})
        assertTrue(registry.claimCleanup(pair))
        assertTrue(registry.retireConnectionIfCleaning(pair) {})
        registry.finishCleanup(pair)

        val connect = assertNotNull(registry.begin(ADDRESS.lowercase(), WATCH_CONNECT) {})
        assertTrue(registry.requestConnectIfPending(connect) {})
        assertNull(registry.begin(ADDRESS, PAIR) {})
    }

    @Test
    fun `cleanup timeout stays busy until retirement is acknowledged`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, WATCH_CONNECT) {})

        assertTrue(registry.requestConnectIfPending(attempt) {})
        assertTrue(registry.claimCleanup(attempt))
        assertTrue(registry.retireConnectionIfCleaning(attempt) {})

        // A timeout only stops waiting for libpebble3; it must not relinquish address ownership.
        assertNull(registry.begin(ADDRESS.lowercase(), PAIR) {})
        registry.finishCleanup(attempt)
        assertNotNull(registry.begin(ADDRESS.lowercase(), PAIR) {})
    }

    @Test
    fun `replacement connection waits until stale cleanup releases the address`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val old = assertNotNull(registry.begin(ADDRESS, WATCH_CONNECT) {})
        assertTrue(registry.claimCleanup(old))
        assertTrue(registry.retireConnectionIfCleaning(old) {})

        assertNull(registry.begin(ADDRESS.lowercase(), PAIR) {})
        registry.finishCleanup(old)
        val replacement = assertNotNull(registry.begin(ADDRESS.lowercase(), PAIR) {})
        assertTrue(registry.complete(replacement))
    }

    @Test
    fun `cancel all claims and cancels every pending operation`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val cancelled = mutableListOf<String>()
        val first = assertNotNull(registry.begin(ADDRESS, PAIR) { cancelled += "first" })
        val second = assertNotNull(
            registry.begin("00:11:22:33:44:55", PAIR) { cancelled += "second" },
        )

        val claimed = registry.claimAllCleanup()
        claimed.forEach { it.cancelOperation() }

        assertEquals(listOf(first, second), claimed)
        assertEquals(listOf("first", "second"), cancelled)
        assertTrue(registry.isCleaning(first))
        assertTrue(registry.isCleaning(second))
    }

    @Test
    fun `cancel all joins cleanup already claimed by operation cancellation`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        assertTrue(registry.claimCleanup(attempt))

        assertEquals(listOf(attempt), registry.claimAllCleanup())
        var retirements = 0
        assertTrue(registry.retireConnectionIfCleaning(attempt) { retirements++ })
        assertFalse(registry.retireConnectionIfCleaning(attempt) { retirements++ })
        // No goal was published, so no cancellation callback is needed.
        assertEquals(0, retirements)
    }

    @Test
    fun `retirement waiter is single owner until acknowledgement`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val old = assertNotNull(registry.begin(ADDRESS, WATCH_CONNECT) {})
        assertTrue(registry.requestConnectIfPending(old) {})
        assertTrue(registry.claimCleanup(old))
        assertTrue(registry.retireConnectionIfCleaning(old) {})
        assertEquals(
            PrimaryConnectionCleanupState.RETIRING,
            registry.cleanupState(old),
        )
        assertTrue(registry.claimRetirementWait(old))
        assertFalse(registry.claimRetirementWait(old))
        assertNull(registry.begin(ADDRESS, PAIR) {})
        registry.finishCleanup(old)
        val next = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        assertTrue(registry.complete(next))
    }

    @Test
    fun `retirement acknowledgement cannot overtake goal cancellation`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val attempt = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        assertTrue(registry.requestConnectIfPending(attempt) {})
        assertTrue(registry.claimCleanup(attempt))
        val cancellationEntered = CountDownLatch(1)
        val allowCancellation = CountDownLatch(1)
        val waiterStarted = CountDownLatch(1)
        val waiterFinished = CountDownLatch(1)
        var waiterClaimed = false

        val retirement = thread(start = true) {
            registry.retireConnectionIfCleaning(attempt) {
                cancellationEntered.countDown()
                assertTrue(allowCancellation.await(5, TimeUnit.SECONDS))
            }
        }
        assertTrue(cancellationEntered.await(5, TimeUnit.SECONDS))
        val waiter = thread(start = true) {
            waiterStarted.countDown()
            waiterClaimed = registry.claimRetirementWait(attempt)
            waiterFinished.countDown()
        }
        assertTrue(waiterStarted.await(5, TimeUnit.SECONDS))
        assertFalse(waiterFinished.await(100, TimeUnit.MILLISECONDS))

        allowCancellation.countDown()
        retirement.join()
        waiter.join()
        assertTrue(waiterClaimed)
    }

    @Test
    fun `cancel pairing filters watch connect attempts`() {
        val registry = PrimaryConnectionAttemptRegistry()
        val pair = assertNotNull(registry.begin(ADDRESS, PAIR) {})
        val connect = assertNotNull(
            registry.begin("00:11:22:33:44:55", WATCH_CONNECT) {},
        )

        assertEquals(listOf(pair), registry.claimAllCleanup(PAIR))
        assertFalse(registry.isCleaning(connect))
        assertTrue(registry.complete(connect))
    }

    private companion object {
        const val ADDRESS = "AA:BB:CC:DD:EE:FF"
        val PAIR = PrimaryConnectionAttemptKind.PAIR
        val WATCH_CONNECT = PrimaryConnectionAttemptKind.WATCH_CONNECT
    }
}
