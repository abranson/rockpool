/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals

class RockpoolConnectCoordinatorTest {
    @Test
    fun `new request waits for cancelled rediscovery cleanup before starting`() = runBlocking {
        val firstStarted = CompletableDeferred<Unit>()
        val allowFirstCleanup = CompletableDeferred<Unit>()
        val secondStarted = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val coordinator = RockpoolConnectCoordinator(scope) { address ->
                events += "start:$address"
                if (address == "first") {
                    firstStarted.complete(Unit)
                    try {
                        CompletableDeferred<Unit>().await()
                    } finally {
                        withContext(NonCancellable) {
                            allowFirstCleanup.await()
                            events += "clean:first"
                        }
                    }
                } else {
                    events += "connect:$address"
                    secondStarted.complete(Unit)
                }
            }

            coordinator.start("first")
            firstStarted.await()
            coordinator.start("second")
            assertEquals(listOf("start:first"), events)

            allowFirstCleanup.complete(Unit)
            withTimeout(1_000) { secondStarted.await() }
            assertEquals(
                listOf("start:first", "clean:first", "start:second", "connect:second"),
                events,
            )
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `only newest queued request runs`() = runBlocking {
        val firstStarted = CompletableDeferred<Unit>()
        val allowFirstCleanup = CompletableDeferred<Unit>()
        val thirdStarted = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val coordinator = RockpoolConnectCoordinator(scope) { address ->
                events += address
                if (address == "first") {
                    firstStarted.complete(Unit)
                    try {
                        CompletableDeferred<Unit>().await()
                    } finally {
                        withContext(NonCancellable) { allowFirstCleanup.await() }
                    }
                } else if (address == "third") {
                    thirdStarted.complete(Unit)
                }
            }

            coordinator.start("first")
            firstStarted.await()
            coordinator.start("second")
            coordinator.start("third")
            allowFirstCleanup.complete(Unit)
            withTimeout(1_000) { thirdStarted.await() }

            assertEquals(listOf("first", "third"), events)
        } finally {
            scope.cancel()
        }
    }
}
