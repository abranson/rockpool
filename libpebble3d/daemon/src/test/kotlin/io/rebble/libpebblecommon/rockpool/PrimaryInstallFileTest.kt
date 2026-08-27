/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.runBlocking
import java.io.ByteArrayInputStream
import java.nio.file.Files
import kotlin.io.path.deleteIfExists
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse

class PrimaryInstallFileTest {
    @Test
    fun `staged install file is private and preserves bytes`() = runBlocking {
        val directory = Files.createTempDirectory("rockpool-install-test-")
        val expected = ByteArray(130_000) { (it % 251).toByte() }
        val staged = stageInstallFile(
            input = ByteArrayInputStream(expected),
            suffix = ".pbw",
            maximumBytes = expected.size.toLong(),
            directory = directory,
        )
        try {
            assertContentEquals(expected, Files.readAllBytes(staged))
            assertFalse(Files.getPosixFilePermissions(staged).any { it.name.startsWith("GROUP_") })
            assertFalse(Files.getPosixFilePermissions(staged).any { it.name.startsWith("OTHERS_") })
        } finally {
            staged.deleteIfExists()
            directory.deleteIfExists()
        }
    }

    @Test
    fun `empty and oversized install files are removed`() = runBlocking {
        val directory = Files.createTempDirectory("rockpool-install-test-")
        try {
            assertFailsWith<InvalidInstallFileException> {
                stageInstallFile(ByteArrayInputStream(byteArrayOf()), ".pbl", 10, directory)
            }
            assertFailsWith<InvalidInstallFileException> {
                stageInstallFile(ByteArrayInputStream(ByteArray(11)), ".pbz", 10, directory)
            }
            assertFalse(Files.list(directory).use { it.findAny().isPresent })
        } finally {
            directory.deleteIfExists()
        }
    }
}
