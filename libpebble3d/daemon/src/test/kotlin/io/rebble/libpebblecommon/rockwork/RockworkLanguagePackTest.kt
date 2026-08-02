/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.connection.ConnectedPebbleDevice
import io.rebble.libpebblecommon.connection.fakeWatch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertTrue

class RockworkLanguagePackTest {
    @Test
    fun `legacy source parser distinguishes downloads and local files`() {
        val remote = assertIs<RockworkLanguagePackSource.Remote>(
            parseRockworkLanguagePackSource(
                "https://binaries.rebble.io/lp/OHRO2oz-it_IT.pbl",
            ),
        )
        assertEquals("OHRO2oz-it_IT", remote.name)

        val fileUrl = assertIs<RockworkLanguagePackSource.Local>(
            parseRockworkLanguagePackSource("file:///tmp/Italian%20pack.pbl"),
        )
        assertEquals("/tmp/Italian pack.pbl", fileUrl.path.toString())
        assertEquals("Italian pack", fileUrl.name)

        val local = assertIs<RockworkLanguagePackSource.Local>(
            parseRockworkLanguagePackSource("/tmp/de_DE.pbl"),
        )
        assertEquals("de_DE", local.name)

        val rawSpace = assertIs<RockworkLanguagePackSource.Local>(
            parseRockworkLanguagePackSource("/tmp/Italian pack.pbl"),
        )
        assertEquals("/tmp/Italian pack.pbl", rawSpace.path.toString())

        val rawPercent = assertIs<RockworkLanguagePackSource.Local>(
            parseRockworkLanguagePackSource("file:///tmp/100% Italian.pbl"),
        )
        assertEquals("/tmp/100% Italian.pbl", rawPercent.path.toString())

        val relativeColon = assertIs<RockworkLanguagePackSource.Local>(
            parseRockworkLanguagePackSource("custom:Italian pack.pbl"),
        )
        assertEquals("custom:Italian pack.pbl", relativeColon.path.toString())

        assertFailsWith<IllegalArgumentException> {
            parseRockworkLanguagePackSource("ftp://example.test/de_DE.pbl")
        }
    }

    @Test
    fun `installer routes URLs and paths to the matching libpebble overload`() {
        val calls = mutableListOf<String>()

        installRockworkLanguagePack(
            parseRockworkLanguagePackSource("https://example.test/en_US.pbl"),
            installRemote = { url, name -> calls += "remote:$url:$name" },
            installLocal = { path, name -> calls += "local:$path:$name" },
        )
        installRockworkLanguagePack(
            parseRockworkLanguagePackSource("file:///tmp/fr_FR.pbl"),
            installRemote = { url, name -> calls += "remote:$url:$name" },
            installLocal = { path, name -> calls += "local:$path:$name" },
        )

        assertEquals(
            listOf(
                "remote:https://example.test/en_US.pbl:en_US",
                "local:/tmp/fr_FR.pbl:fr_FR",
            ),
            calls,
        )
    }

    @Test
    fun `language status uses the legacy locale version shape and completion edge`() {
        val base = fakeWatch(connected = true) as ConnectedPebbleDevice
        val initial = rockworkLanguageStatus(base)
        assertEquals("en-GB:1", initial.version)
        assertEquals(null, initial.completedInstall)

        val completed = initial.copy(completedInstall = "Italian")
        assertTrue(completed.shouldSignalAfter(initial))
        assertTrue(!completed.shouldSignalAfter(completed))

        val disconnected = rockworkLanguageStatus(fakeWatch(connected = false))
        assertEquals("", disconnected.version)
        assertTrue(disconnected.shouldSignalAfter(completed))
    }
}
