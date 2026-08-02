/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.LibPebbleConfig
import java.util.concurrent.CountDownLatch
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LibPebbleConfigMutationCoordinatorTest {
    @Test
    fun `no-op mutation neither persists nor notifies`() {
        val config = LibPebbleConfig()
        var updates = 0
        var notifications = 0
        val coordinator = LibPebbleConfigMutationCoordinator(
            current = { config },
            update = { updates++ },
        )
        coordinator.addListener { notifications++ }

        coordinator.mutate { it }

        assertEquals(0, updates)
        assertEquals(0, notifications)
    }

    @Test
    fun `serialized mutations preserve fields changed by another caller`() {
        var config = LibPebbleConfig()
        val coordinator = LibPebbleConfigMutationCoordinator(
            current = { config },
            update = { config = it },
        )
        val changes = CopyOnWriteArrayList<LibPebbleConfigUpdate>()
        coordinator.addListener(changes::add)
        val firstEntered = CountDownLatch(1)
        val releaseFirst = CountDownLatch(1)
        val secondStarted = CountDownLatch(1)
        var secondObservedCannedResponses = emptyList<String>()

        val first = thread {
            coordinator.mutate { current ->
                firstEntered.countDown()
                assertTrue(releaseFirst.await(5, TimeUnit.SECONDS))
                current.copy(
                    notificationConfig = current.notificationConfig.copy(
                        cannedResponses = listOf("On my way"),
                    ),
                )
            }
        }
        assertTrue(firstEntered.await(5, TimeUnit.SECONDS))

        val second = thread {
            secondStarted.countDown()
            coordinator.mutate { current ->
                secondObservedCannedResponses = current.notificationConfig.cannedResponses
                current.copy(watchConfig = current.watchConfig.copy(calendarPins = false))
            }
        }
        assertTrue(secondStarted.await(5, TimeUnit.SECONDS))
        releaseFirst.countDown()
        first.join(5_000)
        second.join(5_000)

        assertFalse(first.isAlive)
        assertFalse(second.isAlive)
        assertEquals(listOf("On my way"), secondObservedCannedResponses)
        assertEquals(listOf("On my way"), config.notificationConfig.cannedResponses)
        assertFalse(config.watchConfig.calendarPins)
        assertEquals(2, changes.size)
        val cannedChange = changes.single {
            it.current.notificationConfig.cannedResponses == listOf("On my way") &&
                it.previous.notificationConfig.cannedResponses != listOf("On my way")
        }
        assertEquals(
            LibPebbleConfig().notificationConfig.cannedResponses,
            cannedChange.previous.notificationConfig.cannedResponses,
        )
        val calendarChange = changes.single {
            it.previous.watchConfig.calendarPins && !it.current.watchConfig.calendarPins
        }
        assertEquals(listOf("On my way"), calendarChange.current.notificationConfig.cannedResponses)
    }
}
