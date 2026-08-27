/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlin.test.Test
import kotlin.test.assertEquals

class PrimaryWatchCapabilitiesTest {
    @Test
    fun `implemented watch capabilities are independent of runtime connection state`() {
        assertEquals(
            listOf(
                "watch.timeline",
                "watch.notifications",
                "watch.messaging",
                "watch.health",
                "watch.screenshots",
                "watch.logs",
                "watch.developer-mode",
            ),
            primaryWatchCapabilities(),
        )
    }
}
