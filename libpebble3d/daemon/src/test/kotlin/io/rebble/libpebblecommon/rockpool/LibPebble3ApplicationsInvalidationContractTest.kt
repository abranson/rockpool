/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import java.nio.file.Path
import kotlin.io.path.readText
import kotlin.test.Test
import kotlin.test.assertTrue

/**
 * Applications1 is parameterized by a known watch's hardware platform, while
 * the primary object path is stable for a serial/address.  The current service
 * has no injectable signal sink for [LibPebble3WatchObject], so keep this narrow
 * source-level contract until that seam exists.
 */
class LibPebble3ApplicationsInvalidationContractTest {
    @Test
    fun `watch refresh republishes Applications1 for a stable watch object`() {
        val source = Path.of(
            "src/main/kotlin/io/rebble/libpebblecommon/rockpool/LibPebble3Service.kt",
        ).readText()
        val methodStart = source.indexOf("fun propertiesChangedAll()")
        val methodEnd = source.indexOf("\n        override fun Connect()", methodStart)

        assertTrue(methodStart >= 0, "LibPebble3WatchObject.propertiesChangedAll is missing")
        assertTrue(methodEnd > methodStart, "could not delimit propertiesChangedAll")
        assertTrue(
            "propertiesChanged(APPLICATIONS_INTERFACE" in source.substring(methodStart, methodEnd),
            "a refreshed stable watch object must emit Applications1.Applications",
        )
    }
}
