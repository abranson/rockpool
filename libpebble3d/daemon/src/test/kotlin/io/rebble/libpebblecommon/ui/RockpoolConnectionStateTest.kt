/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RockpoolConnectionStateTest {
    @Test
    fun `failure reason change refreshes the existing failed state`() {
        assertTrue(
            shouldEmitConnectionStateChanged(
                RockpoolConnectionState.FAILED,
                "BLUETOOTH_CONNECTION_FAILED",
                RockpoolConnectionState.FAILED,
                "CONNECTION_TIMEOUT",
            ),
        )
    }

    @Test
    fun `unchanged state and failure reason does not emit`() {
        assertFalse(
            shouldEmitConnectionStateChanged(
                RockpoolConnectionState.FAILED,
                "CONNECTION_TIMEOUT",
                RockpoolConnectionState.FAILED,
                "CONNECTION_TIMEOUT",
            ),
        )
    }
}
