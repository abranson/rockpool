/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.runBlocking
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class AccountLockerUpgradeReconcilerTest {
    @Test
    fun `retired signed-in upgrade completes after reconciliation`() {
        var token = "account-a"
        var marker = "retired"
        val sync = CompletableDeferred<Unit>()
        var requests = 0
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = { requests += 1; sync },
        )

        reconciler.start()
        assertEquals(1, requests)
        assertEquals("retired", marker)

        sync.complete(Unit)
        assertEquals("complete", marker)
    }

    @Test
    fun `account switch retires old reconciliation and only completes new session`() {
        var token = "account-a"
        var marker = "retired"
        val syncA = CompletableDeferred<Unit>()
        val syncB = CompletableDeferred<Unit>()
        val requests = ArrayDeque(listOf(syncA, syncB))
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = { requests.removeFirst() },
        )

        reconciler.start()
        token = "account-b"
        reconciler.accountChanged(AccountSessionChange(token, 1))

        assertTrue(syncA.isCancelled)
        syncA.complete(Unit)
        assertEquals("retired", marker)
        syncB.complete(Unit)
        assertEquals("complete", marker)
    }

    @Test
    fun `logout cancels reconciliation and safely completes upgrade`() {
        var token = "account-a"
        var marker = "retired"
        val sync = CompletableDeferred<Unit>()
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = { sync },
        )

        reconciler.start()
        token = ""
        reconciler.accountChanged(AccountSessionChange(token, 1))

        assertTrue(sync.isCancelled)
        assertEquals("complete", marker)
    }

    @Test
    fun `successful account transition can recover a missing retired marker`() {
        val token = "account-b"
        var marker = ""
        val events = mutableListOf<String>()
        val sync = CompletableDeferred<Unit>()
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = {
                marker = it
                events += it
                true
            },
            requestSync = {
                events += "sync"
                sync
            },
        )

        reconciler.start()
        assertTrue(events.isEmpty())
        reconciler.accountChanged(AccountSessionChange(token, 1))
        assertEquals(listOf("retired", "sync"), events)

        sync.complete(Unit)
        assertEquals(listOf("retired", "sync", "complete"), events)
    }

    @Test
    fun `failed reconciliation retries and completes without a restart`() {
        val token = "account-a"
        var marker = "retired"
        val first = CompletableDeferred<Unit>()
        val second = CompletableDeferred<Unit>()
        val requests = ArrayDeque(listOf(first, second))
        var requestCount = 0
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = {
                requestCount += 1
                requests.removeFirst()
            },
        )

        reconciler.start()
        first.completeExceptionally(IllegalStateException("temporary locker failure"))
        assertEquals("retired", marker)

        reconciler.retryIfPending()
        assertEquals(2, requestCount)
        second.complete(Unit)

        assertEquals("complete", marker)
    }

    @Test
    fun `periodic retry preserves an unfinished reconciliation`() {
        val token = "account-a"
        var marker = "retired"
        val first = CompletableDeferred<Unit>()
        val second = CompletableDeferred<Unit>()
        val requests = ArrayDeque(listOf(first, second))
        var requestCount = 0
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = {
                requestCount += 1
                requests.removeFirst()
            },
        )

        reconciler.start()
        reconciler.retryIfPending()
        assertEquals(1, requestCount)
        assertTrue(first.isActive)

        first.completeExceptionally(IllegalStateException("temporary locker failure"))
        reconciler.retryIfPending()
        reconciler.retryIfPending()

        assertEquals(2, requestCount)
        assertTrue(second.isActive)
        second.complete(Unit)
        assertEquals("complete", marker)
    }

    @Test
    fun `synchronous start failure remains retryable`() {
        val token = "account-a"
        var marker = "retired"
        val sync = CompletableDeferred<Unit>()
        var requestCount = 0
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = {
                requestCount += 1
                if (requestCount == 1) throw IllegalStateException("temporary start failure")
                sync
            },
        )

        reconciler.start()
        assertEquals("retired", marker)

        reconciler.retryIfPending()
        assertEquals(2, requestCount)
        sync.complete(Unit)

        assertEquals("complete", marker)
    }

    @Test
    fun `failed retirement marker is retried before reconciliation`() = runBlocking {
        val token = "account-a"
        var marker = ""
        val sync = CompletableDeferred<Unit>()
        var retirements = 0
        var requests = 0
        val reconciler = reconciler(
            currentToken = { token },
            markerState = { marker },
            setMarker = { marker = it; true },
            requestSync = {
                requests += 1
                sync
            },
        )

        reconciler.recoverIfPending {
            retirements += 1
            false
        }
        assertEquals(1, retirements)
        assertEquals(0, requests)
        assertEquals("", marker)

        reconciler.recoverIfPending {
            retirements += 1
            marker = "retired"
            true
        }
        assertEquals(2, retirements)
        assertEquals(1, requests)
        assertEquals("retired", marker)

        reconciler.recoverIfPending {
            error("a durable retired marker must not replay retirement")
        }
        assertEquals(2, retirements)
        assertEquals(1, requests)

        sync.complete(Unit)
        assertEquals("complete", marker)
    }

    @Test
    fun `completed sync remains owned until its marker callback finishes`() {
        val token = "account-a"
        val marker = AtomicReference("retired")
        val sync = CompletableDeferred<Unit>()
        val unusedReplacement = CompletableDeferred<Unit>()
        val requests = ArrayDeque(listOf(sync, unusedReplacement))
        val requestCount = AtomicInteger()
        val markerCallbackEntered = CountDownLatch(1)
        val releaseMarkerCallback = CountDownLatch(1)
        val retryReadRetired = CountDownLatch(2)
        val reconciler = reconciler(
            currentToken = { token },
            markerState = {
                marker.get().also { value ->
                    if (Thread.currentThread().name == "locker-upgrade-retry" && value == "retired") {
                        retryReadRetired.countDown()
                    }
                }
            },
            setMarker = { value ->
                if (value == "complete") {
                    markerCallbackEntered.countDown()
                    check(releaseMarkerCallback.await(5, TimeUnit.SECONDS))
                }
                marker.set(value)
                true
            },
            requestSync = {
                requestCount.incrementAndGet()
                requests.removeFirst()
            },
        )

        reconciler.start()
        val completion = thread(name = "locker-upgrade-completion") {
            sync.complete(Unit)
        }
        assertTrue(markerCallbackEntered.await(5, TimeUnit.SECONDS))

        val retry = thread(name = "locker-upgrade-retry") {
            reconciler.retryIfPending()
        }
        assertTrue(retryReadRetired.await(5, TimeUnit.SECONDS))
        releaseMarkerCallback.countDown()
        completion.join(5_000)
        retry.join(5_000)

        assertTrue(!completion.isAlive)
        assertTrue(!retry.isAlive)
        assertEquals(1, requestCount.get())
        assertEquals("complete", marker.get())
        assertTrue(unusedReplacement.isActive)
    }

    private fun reconciler(
        currentToken: () -> String,
        markerState: () -> String,
        setMarker: (String) -> Boolean,
        requestSync: () -> CompletableDeferred<Unit>,
    ) = AccountLockerUpgradeReconciler(
        currentToken = currentToken,
        markerState = markerState,
        markRetired = { setMarker("retired") },
        markComplete = { setMarker("complete") },
        requestSync = requestSync,
    )
}
