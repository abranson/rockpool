/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.FakeLibPebble
import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LegacyRockpooldImporterAccountTest {
    @Test
    fun `imports a unanimous token from eligible legacy watch directories`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val addresses = setOf("AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:02")
            addresses.forEach { writeToken(root, it, "shared-token") }
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { addresses })

            importer.importIfNeeded(FakeLibPebble())

            assertEquals("shared-token", settings.get("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `conflicting eligible legacy tokens require explicit sign-in but complete migration`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val addresses = setOf("AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:02")
            writeToken(root, "AA:BB:CC:DD:EE:01", "first-token")
            writeToken(root, "AA:BB:CC:DD:EE:02", "second-token")
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { addresses })

            importer.importIfNeeded(FakeLibPebble())

            assertFalse(settings.contains("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `preserves an existing current OAuth token`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val address = "AA:BB:CC:DD:EE:01"
            writeToken(root, address, "legacy-token")
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked("account.oauthToken", "current-token"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { setOf(address) })

            importer.importIfNeeded(FakeLibPebble())

            assertEquals("current-token", settings.get("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `ignores OAuth tokens outside address-shaped legacy directories`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            writeToken(root, "not-a-watch", "ignored-token")
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { emptySet() })

            importer.importIfNeeded(FakeLibPebble())

            assertFalse(settings.contains("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `retries an eligible account import after a non-regular sync file is repaired`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val address = "AA:BB:CC:DD:EE:01"
            val syncFile = root.resolve(address.replace(':', '_')).resolve("timeline/sync.ini")
            Files.createDirectories(syncFile)
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { setOf(address) })

            importer.importIfNeeded(FakeLibPebble())

            assertFalse(settings.contains("account.oauthToken"))
            assertFalse(importer.isComplete())

            Files.delete(syncFile)
            Files.writeString(syncFile, "oauthToken=repaired-token\n")
            importer.importIfNeeded(FakeLibPebble())

            assertEquals("repaired-token", settings.get("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `retries account import after account-token persistence failure`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val address = "AA:BB:CC:DD:EE:01"
            writeToken(root, address, "retry-token")
            val blockedParent = settingsRoot.resolve("blocked")
            Files.writeString(blockedParent, "not a directory")
            val settings = RockpoolSettings(blockedParent.resolve("rockpool.properties"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { setOf(address) })

            importer.importIfNeeded(FakeLibPebble())

            assertFalse(settings.contains("account.oauthToken"))
            assertFalse(importer.isComplete())

            Files.delete(blockedParent)
            Files.createDirectory(blockedParent)
            importer.importIfNeeded(FakeLibPebble())

            assertEquals("retry-token", settings.get("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `preserves an existing empty current OAuth token`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-account-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val address = "AA:BB:CC:DD:EE:01"
            writeToken(root, address, "legacy-token")
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked("account.oauthToken", ""))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { setOf(address) })

            importer.importIfNeeded(FakeLibPebble())

            assertTrue(settings.contains("account.oauthToken"))
            assertEquals("", settings.get("account.oauthToken"))
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    private fun writeToken(root: Path, address: String, token: String) {
        val directory = root.resolve(address.replace(':', '_')).resolve("timeline")
        Files.createDirectories(directory)
        Files.writeString(directory.resolve("sync.ini"), "oauthToken=$token\n")
    }
}
