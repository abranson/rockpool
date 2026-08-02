/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import java.io.IOException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class AccountTokenMutationsTest {
    @Test
    fun `writes retain invocation order when the later operation starts first`() = runBlocking {
        val mutations = AccountTokenMutations()
        val first = mutations.enqueue("first-token")
        val second = mutations.enqueue("second-token")
        val firstEntered = CompletableDeferred<Unit>()
        val releaseFirst = CompletableDeferred<Unit>()
        val secondEntered = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()

        val secondResult = async {
            second.execute(beginCommit = { true }) { token ->
                events += token
                secondEntered.complete(Unit)
                true
            }
        }
        yield()
        assertFalse(secondEntered.isCompleted)

        val firstResult = async {
            first.execute(beginCommit = { true }) { token ->
                events += token
                firstEntered.complete(Unit)
                releaseFirst.await()
                true
            }
        }
        firstEntered.await()
        assertFalse(secondEntered.isCompleted)
        releaseFirst.complete(Unit)

        assertEquals(AccountTokenMutationResult.Saved, firstResult.await())
        assertEquals(AccountTokenMutationResult.Saved, secondResult.await())
        assertEquals(listOf("first-token", "second-token"), events)
    }

    @Test
    fun `pre-start cancellation releases its FIFO position`() = runBlocking {
        val mutations = AccountTokenMutations()
        val cancelled = mutations.enqueue("cancelled-token")
        val next = mutations.enqueue("next-token")
        val writes = mutableListOf<String>()

        cancelled.release()
        val result = next.execute(beginCommit = { true }) { token ->
            writes += token
            true
        }

        assertEquals(AccountTokenMutationResult.Saved, result)
        assertEquals(listOf("next-token"), writes)
    }

    @Test
    fun `completion hook releases a mutation cancelled before its launch body starts`() =
        runBlocking {
            val mutations = AccountTokenMutations()
            val cancelled = mutations.enqueue("cancelled-token")
            val next = mutations.enqueue("next-token")
            val writes = mutableListOf<String>()
            val cancelledJob = launch(start = CoroutineStart.LAZY) {
                cancelled.execute(beginCommit = { true }) { token ->
                    writes += token
                    true
                }
            }
            cancelledJob.invokeOnCompletion { cancelled.release() }

            cancelledJob.cancelAndJoin()
            val result = next.execute(beginCommit = { true }) { token ->
                writes += token
                true
            }

            assertEquals(AccountTokenMutationResult.Saved, result)
            assertEquals(listOf("next-token"), writes)
        }

    @Test
    fun `cancelling a queued operation cannot let its successor overtake the head`() = runBlocking {
        val mutations = AccountTokenMutations()
        val first = mutations.enqueue("first-token")
        val cancelled = mutations.enqueue("cancelled-token")
        val third = mutations.enqueue("third-token")
        val firstEntered = CompletableDeferred<Unit>()
        val releaseFirst = CompletableDeferred<Unit>()
        val thirdEntered = CompletableDeferred<Unit>()
        val writes = mutableListOf<String>()

        val firstResult = async {
            first.execute(beginCommit = { true }) { token ->
                writes += token
                firstEntered.complete(Unit)
                releaseFirst.await()
                true
            }
        }
        firstEntered.await()
        val cancelledResult = async {
            cancelled.execute(beginCommit = { true }) { token ->
                writes += token
                true
            }
        }
        yield()
        cancelledResult.cancelAndJoin()
        val thirdResult = async {
            third.execute(beginCommit = { true }) { token ->
                writes += token
                thirdEntered.complete(Unit)
                true
            }
        }
        yield()
        assertFalse(thirdEntered.isCompleted)

        releaseFirst.complete(Unit)
        assertEquals(AccountTokenMutationResult.Saved, firstResult.await())
        assertEquals(AccountTokenMutationResult.Saved, thirdResult.await())
        assertTrue(cancelledResult.isCancelled)
        assertEquals(listOf("first-token", "third-token"), writes)
    }

    @Test
    fun `persistence failure advances the next write`() = runBlocking {
        val mutations = AccountTokenMutations()
        val first = mutations.enqueue("first-token")
        val second = mutations.enqueue("second-token")
        val writes = mutableListOf<String>()

        val firstResult = async {
            first.execute(beginCommit = { true }) { token ->
                writes += token
                false
            }
        }
        val secondResult = async {
            second.execute(beginCommit = { true }) { token ->
                writes += token
                true
            }
        }

        assertEquals(
            AccountTokenMutationResult.PersistenceFailed,
            firstResult.await(),
        )
        assertEquals(AccountTokenMutationResult.Saved, secondResult.await())
        assertEquals(listOf("first-token", "second-token"), writes)
    }

    @Test
    fun `persistence exception cannot strand the queue`() = runBlocking {
        val mutations = AccountTokenMutations()
        val first = mutations.enqueue("first-token")
        val second = mutations.enqueue("second-token")
        val writes = mutableListOf<String>()

        val firstResult = async {
            runCatching {
                first.execute(beginCommit = { true }) { token ->
                    writes += token
                    throw IOException("storage unavailable")
                }
            }
        }
        val secondResult = async {
            second.execute(beginCommit = { true }) { token ->
                writes += token
                true
            }
        }

        assertIs<IOException>(firstResult.await().exceptionOrNull())
        assertEquals(AccountTokenMutationResult.Saved, secondResult.await())
        assertEquals(listOf("first-token", "second-token"), writes)
    }

    @Test
    fun `cancel wins before commit without persisting and advances the queue`() = runBlocking {
        val mutations = AccountTokenMutations()
        val cancelled = mutations.enqueue("cancelled-token")
        val next = mutations.enqueue("next-token")
        val writes = mutableListOf<String>()

        val cancelledResult = async {
            cancelled.execute(beginCommit = { false }) { token ->
                writes += token
                true
            }
        }
        val nextResult = async {
            next.execute(beginCommit = { true }) { token ->
                writes += token
                true
            }
        }

        assertEquals(AccountTokenMutationResult.Cancelled, cancelledResult.await())
        assertEquals(AccountTokenMutationResult.Saved, nextResult.await())
        assertEquals(listOf("next-token"), writes)
    }

    @Test
    fun `commit keeps successors blocked through non-cancellable persistence`() = runBlocking {
        val mutations = AccountTokenMutations()
        val first = mutations.enqueue("first-token")
        val second = mutations.enqueue("second-token")
        val firstEntered = CompletableDeferred<Unit>()
        val releaseFirst = CompletableDeferred<Unit>()
        val secondEntered = CompletableDeferred<Unit>()

        val firstResult = async {
            first.execute(beginCommit = { true }) {
                firstEntered.complete(Unit)
                releaseFirst.await()
                true
            }
        }
        firstEntered.await()
        val secondResult = async {
            second.execute(beginCommit = { true }) {
                secondEntered.complete(Unit)
                true
            }
        }
        yield()
        assertFalse(secondEntered.isCompleted)

        releaseFirst.complete(Unit)
        assertEquals(AccountTokenMutationResult.Saved, firstResult.await())
        assertEquals(AccountTokenMutationResult.Saved, secondResult.await())
        assertTrue(secondEntered.isCompleted)
    }
}
