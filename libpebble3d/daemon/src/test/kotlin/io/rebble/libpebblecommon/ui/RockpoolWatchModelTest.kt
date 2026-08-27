/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.metadata.WatchColor
import kotlin.test.Test
import kotlin.test.assertEquals

class RockpoolWatchModelTest {
    @Test
    fun `disconnected known watch uses persisted colour`() {
        assertEquals(
            WatchColor.PebbleTime2BlackGray.protocolNumber,
            rockpoolWatchModel(
                connectedColor = null,
                knownColor = WatchColor.PebbleTime2BlackGray,
            ),
        )
    }

    @Test
    fun `connected watch protocol colour takes precedence`() {
        assertEquals(
            WatchColor.PebbleRound2Gold14.protocolNumber,
            rockpoolWatchModel(
                connectedColor = WatchColor.PebbleRound2Gold14,
                knownColor = WatchColor.ClassicBlack,
            ),
        )
    }

    @Test
    fun `unknown or discovered watch has no model`() {
        assertEquals(0, rockpoolWatchModel(connectedColor = null, knownColor = null))
    }
}
