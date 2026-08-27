/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import kotlin.test.Test
import kotlin.test.assertEquals

class FirmwareProgressObserverTest {
    @Test
    fun `observer publishes progress and rejects a retired update session`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            var changes = 0
            val observer = FirmwareProgressObserver(scope) { changes++ }
            val first = MutableStateFlow(0.1f)
            val firstSource = Any()
            observer.update(firstSource, first)
            yield()
            first.value = 0.4f
            yield()
            assertEquals(1, changes)

            val second = MutableStateFlow(0.6f)
            val secondSource = Any()
            observer.update(secondSource, second)
            yield()
            first.value = 0.8f
            yield()
            assertEquals(1, changes)
            second.value = 0.9f
            yield()
            assertEquals(2, changes)

            observer.close()
            second.value = 1.0f
            yield()
            assertEquals(2, changes)
        } finally {
            scope.cancel()
        }
    }
}
