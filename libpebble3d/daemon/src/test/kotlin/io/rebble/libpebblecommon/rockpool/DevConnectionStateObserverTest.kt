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

class DevConnectionStateObserverTest {
    @Test
    fun `observer emits real transitions and retires replaced sources`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val emitted = mutableListOf<Boolean>()
            val observer = DevConnectionStateObserver(scope, emitted::add)
            val firstSource = Any()
            val first = MutableStateFlow(false)

            observer.update(firstSource, first)
            yield()
            assertEquals(emptyList(), emitted)

            first.value = true
            yield()
            first.value = true
            yield()
            first.value = false
            yield()
            assertEquals(listOf(true, false), emitted)

            first.value = true
            yield()
            val secondSource = Any()
            val second = MutableStateFlow(true)
            observer.update(secondSource, second)
            yield()
            assertEquals(listOf(true, false, true, false, true), emitted)

            first.value = false
            first.value = true
            yield()
            assertEquals(listOf(true, false, true, false, true), emitted)

            observer.close()
            yield()
            assertEquals(listOf(true, false, true, false, true, false), emitted)
            second.value = true
            yield()
            assertEquals(listOf(true, false, true, false, true, false), emitted)
        } finally {
            scope.cancel()
        }
    }
}
