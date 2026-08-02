/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RockworkConnectionStateTest {
    @Test
    fun `failure reason change refreshes the existing failed state`() {
        assertTrue(
            shouldEmitConnectionStateChanged(
                RockworkConnectionState.FAILED,
                "BLUETOOTH_CONNECTION_FAILED",
                RockworkConnectionState.FAILED,
                "CONNECTION_TIMEOUT",
            ),
        )
    }

    @Test
    fun `unchanged state and failure reason does not emit`() {
        assertFalse(
            shouldEmitConnectionStateChanged(
                RockworkConnectionState.FAILED,
                "CONNECTION_TIMEOUT",
                RockworkConnectionState.FAILED,
                "CONNECTION_TIMEOUT",
            ),
        )
    }
}
