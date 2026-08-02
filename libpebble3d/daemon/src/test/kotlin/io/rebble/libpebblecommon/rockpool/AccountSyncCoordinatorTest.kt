/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import java.io.IOException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class AccountSyncCoordinatorTest {
    @Test
    fun `synchronous start failure releases the slot for a retry`() = runBlocking {
        val states = mutableListOf<String>()
        val coordinator = AccountSyncCoordinator(states::add)

        val failed = coordinator.synchronize(beginCommit = { true }) {
            throw IOException("could not start")
        }
        val retried = coordinator.synchronize(beginCommit = { true }) {
            CompletableDeferred(Unit)
        }

        assertIs<AccountSyncResult.Failed>(failed)
        assertEquals(AccountSyncResult.Completed, retried)
        assertEquals("idle", coordinator.state())
        assertEquals(listOf("failed", "syncing", "idle"), states)
    }

    @Test
    fun `duplicate active sync is busy and does not start twice`() = runBlocking {
        val coordinator = AccountSyncCoordinator()
        val running = CompletableDeferred<Unit>()
        var starts = 0
        val first = async {
            coordinator.synchronize(beginCommit = { true }) {
                starts += 1
                running
            }
        }
        while (coordinator.state() != "syncing") yield()

        val duplicate = coordinator.synchronize(beginCommit = { true }) {
            starts += 1
            CompletableDeferred(Unit)
        }
        assertEquals(AccountSyncResult.Busy, duplicate)
        assertEquals(1, starts)

        running.complete(Unit)
        assertEquals(AccountSyncResult.Completed, first.await())
        assertEquals("idle", coordinator.state())
    }

    @Test
    fun `automatic login sync shares the explicit synchronization slot`() = runBlocking {
        val coordinator = AccountSyncCoordinator()
        val running = CompletableDeferred<Unit>()
        var starts = 0
        coordinator.accountChanged(1)
        val automatic = async {
            coordinator.synchronize(beginCommit = { true }) {
                starts += 1
                running
            }
        }
        while (coordinator.state() != "syncing") yield()

        val explicit = coordinator.synchronize(beginCommit = { true }) {
            starts += 1
            CompletableDeferred(Unit)
        }

        assertEquals(AccountSyncResult.Busy, explicit)
        assertEquals(1, starts)
        running.complete(Unit)
        assertEquals(AccountSyncResult.Completed, automatic.await())
        assertEquals("idle", coordinator.state())
    }

    @Test
    fun `account replacement cancels old sync and permits the new account`() = runBlocking {
        val coordinator = AccountSyncCoordinator()
        val oldSync = CompletableDeferred<Unit>()
        val oldResult = async {
            coordinator.synchronize(beginCommit = { true }) { oldSync }
        }
        while (coordinator.state() != "syncing") yield()

        coordinator.accountChanged(1)
        assertTrue(oldSync.isCancelled)
        assertEquals(AccountSyncResult.AccountChanged, oldResult.await())
        assertEquals("idle", coordinator.state())

        assertEquals(
            AccountSyncResult.Completed,
            coordinator.synchronize(beginCommit = { true }) { CompletableDeferred(Unit) },
        )
    }

    @Test
    fun `account replacement during synchronous start rejects returned work`() = runBlocking {
        val coordinator = AccountSyncCoordinator()
        val obsolete = CompletableDeferred<Unit>()

        val result = coordinator.synchronize(beginCommit = { true }) {
            coordinator.accountChanged(1)
            obsolete
        }

        assertEquals(AccountSyncResult.AccountChanged, result)
        assertTrue(obsolete.isCancelled)
        assertEquals("idle", coordinator.state())
    }

    @Test
    fun `cancel before commit does not start locker sync`() = runBlocking {
        val states = mutableListOf<String>()
        val coordinator = AccountSyncCoordinator(states::add)
        var starts = 0

        val result = coordinator.synchronize(beginCommit = { false }) {
            starts += 1
            CompletableDeferred(Unit)
        }

        assertEquals(AccountSyncResult.Cancelled, result)
        assertEquals(0, starts)
        assertEquals("idle", coordinator.state())
        assertTrue(states.isEmpty())
    }

    @Test
    fun `commit boundary is crossed before synchronous locker sync start`() = runBlocking {
        val coordinator = AccountSyncCoordinator()
        val events = mutableListOf<String>()
        var acceptsCancellation = true
        var cancellationAcceptedDuringStart = true

        val result = coordinator.synchronize(
            beginCommit = {
                events += "commit"
                if (!acceptsCancellation) {
                    false
                } else {
                    acceptsCancellation = false
                    true
                }
            },
            start = {
                assertEquals(listOf("commit"), events)
                cancellationAcceptedDuringStart = acceptsCancellation
                events += "start"
                CompletableDeferred(Unit)
            },
        )

        assertEquals(AccountSyncResult.Completed, result)
        assertFalse(cancellationAcceptedDuringStart)
        assertEquals(listOf("commit", "start"), events)
    }
}
