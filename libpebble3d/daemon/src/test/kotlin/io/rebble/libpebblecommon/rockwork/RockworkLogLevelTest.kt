/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import co.touchlab.kermit.Severity
import kotlin.test.Test
import kotlin.test.assertEquals

class RockworkLogLevelTest {
    @Test
    fun `legacy log levels retain the QML Debug Warning Critical indices`() {
        assertEquals(Severity.Debug, rockworkLogSeverity(0))
        assertEquals(Severity.Info, rockworkLogSeverity(1))
        assertEquals(Severity.Error, rockworkLogSeverity(2))
        assertEquals(0, normalizedRockworkLogLevel(-1))
        assertEquals(2, normalizedRockworkLogLevel(99))
    }
}
