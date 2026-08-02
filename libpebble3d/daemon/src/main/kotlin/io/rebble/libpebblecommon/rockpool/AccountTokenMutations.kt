/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Assigns account-token writes a FIFO position synchronously at either D-Bus
 * method entry, before lazy primary operations or compatibility dispatch can
 * acquire the durable account mutex out of order.
 */
internal class AccountTokenMutations {
    private val lock = Any()
    private var tail = CompletableDeferred(Unit)

    fun enqueue(token: String): AccountTokenMutation = synchronized(lock) {
        val predecessor = tail
        val completion = CompletableDeferred<Unit>()
        tail = completion
        AccountTokenMutation(token, predecessor, completion)
    }
}

internal class AccountTokenMutation internal constructor(
    private val token: String,
    private val predecessor: CompletableDeferred<Unit>,
    private val completion: CompletableDeferred<Unit>,
) {
    private val released = AtomicBoolean(false)

    suspend fun execute(
        beginCommit: () -> Boolean,
        persist: suspend (String) -> Boolean,
    ): AccountTokenMutationResult {
        try {
            predecessor.await()
            currentCoroutineContext().ensureActive()
            if (!beginCommit()) return AccountTokenMutationResult.Cancelled
            return withContext(NonCancellable) {
                if (persist(token)) {
                    AccountTokenMutationResult.Saved
                } else {
                    AccountTokenMutationResult.PersistenceFailed
                }
            }
        } finally {
            release()
        }
    }

    /**
     * Idempotently retire this FIFO position. If its predecessor is still
     * active, successors remain blocked until that predecessor finishes.
     */
    fun release() {
        if (!released.compareAndSet(false, true)) return
        predecessor.invokeOnCompletion { completion.complete(Unit) }
    }
}

internal enum class AccountTokenMutationResult {
    Saved,
    Cancelled,
    PersistenceFailed,
}
