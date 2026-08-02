/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RockworkFirmwareStatusTest {
    @Test
    fun `firmware status invalidates every exposed metadata change`() {
        val absent = status(available = false)
        val releaseA = status(
            available = true,
            candidateVersion = "4.4.0",
            releaseNotes = "Release A",
        )
        val releaseB = status(
            available = true,
            candidateVersion = "4.5.0",
            releaseNotes = "Release B",
        )
        val amendedReleaseB = status(
            available = true,
            candidateVersion = "4.5.0",
            releaseNotes = "Amended release B",
        )

        assertFalse(absent.shouldSignalAfter(absent))
        assertTrue(releaseA.shouldSignalAfter(absent), "absent -> present must invalidate")
        assertTrue(absent.shouldSignalAfter(releaseA), "present -> absent must invalidate")
        assertTrue(
            releaseB.shouldSignalAfter(releaseA),
            "a replacement candidate must invalidate even while availability stays true",
        )
        assertTrue(
            amendedReleaseB.shouldSignalAfter(releaseB),
            "changed release notes must invalidate even while availability and version stay equal",
        )
        assertFalse(releaseB.shouldSignalAfter(releaseB))
    }

    /**
     * Keep this regression compileable while production lacks the firmware-status projection.
     * The test remains red until RockworkService tracks the complete exposed tuple instead of
     * only the availability boolean, and then exercises that production type reflectively.
     */
    private fun status(
        available: Boolean,
        candidateVersion: String = "",
        releaseNotes: String = "",
    ): FirmwareStatus {
        val type = Class.forName(
            "io.rebble.libpebblecommon.compat.rockwork.RockworkFirmwareStatus",
        )
        val constructor = type.getDeclaredConstructor(
            Boolean::class.javaPrimitiveType,
            String::class.java,
            String::class.java,
        ).apply { isAccessible = true }
        val value = constructor.newInstance(available, candidateVersion, releaseNotes)
        val shouldSignalAfter = type.getDeclaredMethod("shouldSignalAfter", type)
            .apply { isAccessible = true }
        return FirmwareStatus(value, shouldSignalAfter)
    }

    private class FirmwareStatus(
        private val value: Any,
        private val shouldSignalAfter: java.lang.reflect.Method,
    ) {
        fun shouldSignalAfter(previous: FirmwareStatus): Boolean =
            shouldSignalAfter.invoke(value, previous.value) as Boolean
    }
}
