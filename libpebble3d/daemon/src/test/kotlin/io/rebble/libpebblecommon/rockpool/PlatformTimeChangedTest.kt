/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.datetime.TimeZone
import java.util.TimeZone as JavaTimeZone
import kotlin.test.Test
import kotlin.test.assertEquals

class PlatformTimeChangedTest {
    @Test
    fun timeChangeRefreshesCachedZoneBeforeSync() {
        val originalZone = JavaTimeZone.getDefault()
        val originalProperty = System.getProperty("user.timezone")
        try {
            System.clearProperty("user.timezone")
            JavaTimeZone.setDefault(null)
            val systemZone = TimeZone.currentSystemDefault()
            val staleZone = if (systemZone.id == "Pacific/Honolulu") {
                "Europe/Helsinki"
            } else {
                "Pacific/Honolulu"
            }
            // Reproduce a daemon which cached the timezone before the phone moved.
            System.setProperty("user.timezone", staleZone)
            JavaTimeZone.setDefault(JavaTimeZone.getTimeZone(staleZone))
            assertEquals(staleZone, TimeZone.currentSystemDefault().id)

            var callbacks = 0
            platformTimeChanged {
                callbacks++
                assertEquals(systemZone, TimeZone.currentSystemDefault())
                assertEquals(systemZone.id, JavaTimeZone.getDefault().id)
            }
            assertEquals(1, callbacks)
        } finally {
            if (originalProperty == null) {
                System.clearProperty("user.timezone")
            } else {
                System.setProperty("user.timezone", originalProperty)
            }
            JavaTimeZone.setDefault(originalZone)
        }
    }
}
