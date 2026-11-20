/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class SailfishDeviceActivityTest {
    @Test
    fun `only known active phone state suppresses notifications`() {
        val provider = PlatformProviderController()
        val activity = SailfishDeviceActivity(provider.phoneActive)

        assertFalse(activity.isActiveAndUnlocked())

        provider.phoneStateChanged(PHONE_KNOWN or PHONE_ACTIVE)
        assertTrue(activity.isActiveAndUnlocked())

        provider.phoneStateChanged(PHONE_KNOWN)
        assertFalse(activity.isActiveAndUnlocked())

        provider.phoneStateChanged(PHONE_ACTIVE)
        assertFalse(activity.isActiveAndUnlocked())
    }

    @Test
    fun `an unknown phone state allows notifications again`() {
        val provider = PlatformProviderController()
        val activity = SailfishDeviceActivity(provider.phoneActive)

        provider.phoneStateChanged(PHONE_KNOWN or PHONE_ACTIVE)
        assertTrue(activity.isActiveAndUnlocked())

        provider.phoneStateChanged(0)
        assertFalse(activity.isActiveAndUnlocked())
    }

    private companion object {
        const val PHONE_KNOWN = 4
        const val PHONE_ACTIVE = 8
    }
}
