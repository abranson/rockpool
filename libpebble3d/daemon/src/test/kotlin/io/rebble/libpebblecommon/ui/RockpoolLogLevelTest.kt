/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Severity
import kotlin.test.Test
import kotlin.test.assertEquals

class RockpoolLogLevelTest {
    @Test
    fun `legacy log levels retain the QML Debug Warning Critical indices`() {
        assertEquals(Severity.Debug, rockpoolLogSeverity(0))
        assertEquals(Severity.Info, rockpoolLogSeverity(1))
        assertEquals(Severity.Error, rockpoolLogSeverity(2))
        assertEquals(0, normalizedRockpoolLogLevel(-1))
        assertEquals(2, normalizedRockpoolLogLevel(99))
    }
}
