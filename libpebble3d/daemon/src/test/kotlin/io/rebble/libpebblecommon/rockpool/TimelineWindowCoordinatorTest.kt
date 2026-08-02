/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class TimelineWindowCoordinatorTest {
    @Test
    fun `uses legacy defaults until a watch has a stored window`() {
        withSettings { settings ->
            val window = TimelineWindowCoordinator(settings).windowFor("aa:bb:cc:dd:ee:ff").value

            assertEquals(-2, window.pastDays)
            assertEquals(-3600, window.notificationFadeSeconds)
            assertEquals(7, window.futureDays)
        }
    }

    @Test
    fun `normalizes and persists each watch window independently`() {
        withSettings { settings ->
            val coordinator = TimelineWindowCoordinator(settings)

            assertTrue(coordinator.update("aa:bb:cc:dd:ee:ff", -4, 120, 10))
            assertEquals(
                -120,
                coordinator.windowFor("AA:BB:CC:DD:EE:FF").value.notificationFadeSeconds,
            )
            assertEquals(-2, coordinator.windowFor("11:22:33:44:55:66").value.pastDays)
            assertEquals(-4, coordinator.sourceWindow.value.pastDays)
            assertEquals(-3600, coordinator.sourceWindow.value.notificationFadeSeconds)
            assertEquals(10, coordinator.sourceWindow.value.futureDays)

            val reloaded = TimelineWindowCoordinator(settings)
                .windowFor("aa:bb:cc:dd:ee:ff").value
            assertEquals(-4, reloaded.pastDays)
            assertEquals(-120, reloaded.notificationFadeSeconds)
            assertEquals(10, reloaded.futureDays)
        }
    }

    @Test
    fun `invalid window leaves the current values intact`() {
        withSettings { settings ->
            val coordinator = TimelineWindowCoordinator(settings)
            assertTrue(coordinator.update("AA:BB:CC:DD:EE:FF", -3, -900, 4))

            assertFalse(coordinator.update("AA:BB:CC:DD:EE:FF", 0, -900, 4))
            val window = coordinator.windowFor("AA:BB:CC:DD:EE:FF").value
            assertEquals(-3, window.pastDays)
            assertEquals(-900, window.notificationFadeSeconds)
            assertEquals(4, window.futureDays)
        }
    }

    @Test
    fun `migration reload updates an already published watch flow`() {
        withSettings { settings ->
            val coordinator = TimelineWindowCoordinator(settings)
            val published = coordinator.windowFor("AA:BB:CC:DD:EE:FF")
            val prefix = "AA_BB_CC_DD_EE_FF.timeline"

            assertTrue(
                settings.setAllChecked(
                    mapOf(
                        "$prefix.start" to "-8",
                        "$prefix.fade" to "-600",
                        "$prefix.end" to "14",
                    ),
                ),
            )
            coordinator.reloadPersisted()

            assertTrue(published === coordinator.windowFor("AA:BB:CC:DD:EE:FF"))
            assertEquals(-8, published.value.pastDays)
            assertEquals(-600, published.value.notificationFadeSeconds)
            assertEquals(14, published.value.futureDays)
        }
    }

    private fun withSettings(block: (RockpoolSettings) -> Unit) {
        val directory = Files.createTempDirectory("rockpool-timeline-window-")
        try {
            block(RockpoolSettings(directory.resolve("rockpool.properties")))
        } finally {
            directory.toFile().deleteRecursively()
        }
    }
}
