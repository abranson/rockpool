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
import kotlin.uuid.Uuid

class RunningAppStateObserverTest {
    @Test
    fun `observer publishes transitions and ignores a retired connection`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            var changes = 0
            val observer = RunningAppStateObserver(scope) { changes++ }
            val firstSource = Any()
            val first = MutableStateFlow<Uuid?>(null)
            val firstApp = Uuid.parse("11111111-1111-4111-8111-111111111111")
            val secondApp = Uuid.parse("22222222-2222-4222-8222-222222222222")

            observer.update(firstSource, first)
            yield()
            assertEquals(0, changes)
            first.value = firstApp
            yield()
            assertEquals(1, changes)

            val secondSource = Any()
            val second = MutableStateFlow<Uuid?>(secondApp)
            observer.update(secondSource, second)
            yield()
            assertEquals(2, changes)
            first.value = null
            yield()
            assertEquals(2, changes)

            second.value = null
            yield()
            assertEquals(3, changes)
            observer.close()
            yield()
            assertEquals(3, changes)
        } finally {
            scope.cancel()
        }
    }
}
