/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.ConnectionFailureInfo
import io.rebble.libpebblecommon.connection.ConnectionFailureReason
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ConnectionAttemptCompletionTest {
    @Test
    fun `stale initial failure is not terminal for a retry`() {
        val initial = ConnectionFailureInfo(ConnectionFailureReason.ConnectTimeout, times = 1)

        assertFalse(
            connectionAttemptIsTerminal(
                isConnected = false,
                initialFailure = initial,
                currentFailure = initial,
            ),
        )
    }

    @Test
    fun `incremented failure is terminal for a retry`() {
        val initial = ConnectionFailureInfo(ConnectionFailureReason.ConnectTimeout, times = 1)

        assertTrue(
            connectionAttemptIsTerminal(
                isConnected = false,
                initialFailure = initial,
                currentFailure = initial.copy(times = 2),
            ),
        )
    }

    @Test
    fun `different failure is terminal for a retry`() {
        val initial = ConnectionFailureInfo(ConnectionFailureReason.ConnectTimeout, times = 1)

        assertTrue(
            connectionAttemptIsTerminal(
                isConnected = false,
                initialFailure = initial,
                currentFailure = ConnectionFailureInfo(ConnectionFailureReason.NegotiationFailed, times = 1),
            ),
        )
    }

    @Test
    fun `connected is terminal despite a stale failure`() {
        assertTrue(
            connectionAttemptIsTerminal(
                isConnected = true,
                initialFailure = ConnectionFailureInfo(ConnectionFailureReason.ConnectTimeout, times = 1),
                currentFailure = ConnectionFailureInfo(ConnectionFailureReason.ConnectTimeout, times = 1),
            ),
        )
    }
}
