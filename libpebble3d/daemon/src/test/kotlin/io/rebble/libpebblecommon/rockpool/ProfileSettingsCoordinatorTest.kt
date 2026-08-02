/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ProfileSettingsCoordinatorTest {
    @Test
    fun `updates primary and compatibility records atomically`() {
        val writes = mutableListOf<Map<String, String>>()
        val changed = mutableListOf<ProfileSettingsChange>()
        val coordinator = ProfileSettingsCoordinator(
            objectIdForWatch = { _, _ -> "stable-id" },
            persist = { values -> writes += values; true },
        )
        coordinator.addListener(changed::add)

        assertTrue(
            coordinator.updateForWatch(
                serial = "SERIAL",
                address = "aa:bb:cc:dd:ee:ff",
                profiles = mapOf("connected" to "meeting"),
            ),
        )
        assertEquals(
            mapOf(
                "watch.stable-id.profiles.connected" to "meeting",
                "AA_BB_CC_DD_EE_FF.profile.connected" to "meeting",
            ),
            writes.single(),
        )
        assertEquals(
            listOf(ProfileSettingsChange("AA:BB:CC:DD:EE:FF", setOf("connected"))),
            changed,
        )
    }

    @Test
    fun `failed persistence emits no profile change`() {
        val changed = mutableListOf<ProfileSettingsChange>()
        val coordinator = ProfileSettingsCoordinator(
            objectIdForWatch = { _, _ -> "stable-id" },
            persist = { false },
        )
        coordinator.addListener(changed::add)

        assertFalse(
            coordinator.updateByObjectId(
                objectId = "stable-id",
                address = "AA:BB:CC:DD:EE:FF",
                profiles = mapOf("disconnected" to "silent"),
            ),
        )
        assertEquals(emptyList(), changed)
    }
}
