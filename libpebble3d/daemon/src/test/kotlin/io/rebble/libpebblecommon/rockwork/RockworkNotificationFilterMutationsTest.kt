/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.rockpool.NotificationFilterMutation
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.time.Duration.Companion.seconds

class RockworkNotificationFilterMutationsTest {
    @Test
    fun `rapid set then forget executes in D-Bus arrival order`() = runBlocking {
        val workerScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val setEntered = CompletableDeferred<Unit>()
        val releaseSet = CompletableDeferred<Unit>()
        val calls = mutableListOf<String>()
        try {
            val mutations = RockworkNotificationFilterMutations(
                scope = workerScope,
                setFilter = { source, enabled, name ->
                    synchronized(calls) { calls += "set:$source:$enabled:$name" }
                    setEntered.complete(Unit)
                    releaseSet.await()
                    mutation()
                },
                forgetFilter = { source ->
                    synchronized(calls) { calls += "forget:$source" }
                    mutation()
                },
                applicationName = { "Chat" },
            )

            val set = mutations.enqueueSet("org.example.chat", 2)
            val forget = mutations.enqueueForget("org.example.chat")
            withTimeout(5.seconds) { setEntered.await() }

            assertEquals(
                listOf("set:org.example.chat:2:Chat"),
                synchronized(calls) { calls.toList() },
            )

            releaseSet.complete(Unit)
            assertNotNull(withTimeout(5.seconds) { set.await() })
            assertNotNull(withTimeout(5.seconds) { forget.await() })
            assertEquals(
                listOf("set:org.example.chat:2:Chat", "forget:org.example.chat"),
                synchronized(calls) { calls.toList() },
            )
        } finally {
            workerScope.cancel()
        }
    }

    @Test
    fun `failed mutation does not strand the following command`() = runBlocking {
        val workerScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val calls = mutableListOf<String>()
        try {
            val mutations = RockworkNotificationFilterMutations(
                scope = workerScope,
                setFilter = { source, _, _ ->
                    synchronized(calls) { calls += "set:$source" }
                    error("simulated write failure")
                },
                forgetFilter = { source ->
                    synchronized(calls) { calls += "forget:$source" }
                    mutation()
                },
                applicationName = { null },
            )

            val failed = mutations.enqueueSet("org.example.chat", 0)
            val following = mutations.enqueueForget("org.example.chat")

            assertNull(withTimeout(5.seconds) { failed.await() })
            assertNotNull(withTimeout(5.seconds) { following.await() })
            assertEquals(
                listOf("set:org.example.chat", "forget:org.example.chat"),
                synchronized(calls) { calls.toList() },
            )
        } finally {
            workerScope.cancel()
        }
    }

    private fun mutation() = NotificationFilterMutation(
        activePinsUpdated = true,
        filter = null,
        runtimePolicyUpdated = true,
        applicationStateUpdated = true,
    )
}
