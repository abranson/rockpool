/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.metadata.WatchColor
import kotlin.test.Test
import kotlin.test.assertEquals

class RockworkWatchModelTest {
    @Test
    fun `disconnected known watch uses persisted colour`() {
        assertEquals(
            WatchColor.PebbleTime2BlackGray.protocolNumber,
            rockworkWatchModel(
                connectedColor = null,
                knownColor = WatchColor.PebbleTime2BlackGray,
            ),
        )
    }

    @Test
    fun `connected watch protocol colour takes precedence`() {
        assertEquals(
            WatchColor.PebbleRound2Gold14.protocolNumber,
            rockworkWatchModel(
                connectedColor = WatchColor.PebbleRound2Gold14,
                knownColor = WatchColor.ClassicBlack,
            ),
        )
    }

    @Test
    fun `unknown or discovered watch has no model`() {
        assertEquals(0, rockworkWatchModel(connectedColor = null, knownColor = null))
    }
}
