/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.PebbleDevice
import io.rebble.libpebblecommon.connection.fakeWatch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import java.io.IOException
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LegacyRockpooldImporterTimelineTest {
    @Test
    fun `retries each non-regular per-watch INI source after repair`() = runBlocking {
        val sources = listOf(
            "appsettings.conf" to "[unitsDistance]\nimperialUnits=true\n",
            "notifications.conf" to "[Mail]\nenabled=true\n",
            "canned_messages.conf" to "[Quick/0]\nmsg=Repaired\n",
        )
        sources.forEach { (name, content) ->
            val root = Files.createTempDirectory("legacy-rockpoold-timeline-")
            val settingsRoot = Files.createTempDirectory("rockpool-settings-")
            try {
                val watch = fakeWatch(connected = true) as KnownPebbleDevice
                val directory = root.resolve(watch.identifier.asString.uppercase().replace(':', '_'))
                Files.createDirectories(directory.resolve(name))
                val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
                val importer = LegacyRockpooldImporter(
                    settings = settings,
                    legacyRoot = root,
                    readBondedAddresses = { emptySet() },
                )
                val libPebble = object : LibPebble by FakeLibPebble() {
                    override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))
                }

                importer.importIfNeeded(libPebble)

                assertFalse(importer.isComplete(), "$name must keep v1 retryable")

                Files.delete(directory.resolve(name))
                Files.writeString(directory.resolve(name), content)
                importer.importIfNeeded(libPebble)

                assertTrue(importer.isComplete(), "$name repair must complete v1")
                when (name) {
                    "appsettings.conf" -> assertEquals("true", settings.get("imperialUnits"))
                    "notifications.conf" -> assertTrue(
                        settings.entries("watch.").any { (key, value) ->
                            key.endsWith(".source") && value == "Mail"
                        },
                    )
                    "canned_messages.conf" -> {
                        assertEquals("Quick", settings.get("canned.keys"))
                        assertEquals("Repaired", settings.get("canned.Quick"))
                    }
                }
            } finally {
                root.toFile().deleteRecursively()
                settingsRoot.toFile().deleteRecursively()
            }
        }
    }

    @Test
    fun `missing per-watch INI sources are benign`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-timeline-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val watch = fakeWatch(connected = true) as KnownPebbleDevice
            Files.createDirectories(root.resolve(watch.identifier.asString.uppercase().replace(':', '_')))
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val libPebble = object : LibPebble by FakeLibPebble() {
                override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))
            }
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { emptySet() })

            importer.importIfNeeded(libPebble)

            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `does not recreate notification fallback after empty canonical policy`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-notifications-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val watch = fakeWatch(connected = true) as KnownPebbleDevice
            val directory = root.resolve(watch.identifier.asString.uppercase().replace(':', '_'))
            Files.createDirectories(directory)
            Files.writeString(
                directory.resolve("notifications.conf"),
                """
                [Mail]
                enabled=false
                """.trimIndent(),
            )
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING, "true"))
            val libPebble = object : LibPebble by FakeLibPebble() {
                override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))
            }

            val importer = LegacyRockpooldImporter(
                settings,
                root,
                readBondedAddresses = { emptySet() },
            )
            importer.importIfNeeded(libPebble)

            assertTrue(importer.isComplete())
            assertEquals("true", settings.get(GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING))
            assertTrue(settings.entries("watch.").none { it.key.contains(".notifications.") })
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `retries when bonded-address lookup fails`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-timeline-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            var failLookup = true
            val importer = LegacyRockpooldImporter(
                settings = settings,
                legacyRoot = root,
                readBondedAddresses = {
                    if (failLookup) throw IOException("BlueZ lookup failed")
                    emptySet()
                },
            )

            importer.importIfNeeded(FakeLibPebble())
            assertFalse(importer.isComplete())

            failLookup = false
            importer.importIfNeeded(FakeLibPebble())
            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `retries when legacy root disappears during import`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-timeline-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            val watch = fakeWatch(connected = true) as KnownPebbleDevice
            val address = watch.identifier.asString.uppercase()
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val libPebble = object : LibPebble by FakeLibPebble() {
                override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))
            }
            var deleteRoot = true
            val importer = LegacyRockpooldImporter(
                settings = settings,
                legacyRoot = root,
                readBondedAddresses = {
                    if (deleteRoot) root.toFile().deleteRecursively()
                    emptySet()
                },
            )

            importer.importIfNeeded(libPebble)

            assertFalse(importer.isComplete())

            deleteRoot = false
            Files.createDirectories(root.resolve(address.replace(':', '_')))
            importer.importIfNeeded(libPebble)

            assertTrue(importer.isComplete())
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    @Test
    fun `imports a complete legacy timeline window as an atomic normalized group`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-timeline-")
        try {
            val watch = fakeWatch(connected = true) as KnownPebbleDevice
            val address = watch.identifier.asString.uppercase()
            val directory = root.resolve(address.replace(':', '_'))
            Files.createDirectories(directory)
            Files.writeString(
                directory.resolve("appsettings.conf"),
                """
                [timeline]
                pastDays=-4
                eventFadeout=120
                futureDays=10
                """.trimIndent(),
            )
            val settings = RockpoolSettings(root.resolve("rockpool.properties"))
            val libPebble = object : LibPebble by FakeLibPebble() {
                override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))
            }

            LegacyRockpooldImporter(
                settings = settings,
                legacyRoot = root,
                readBondedAddresses = { emptySet() },
            ).importIfNeeded(libPebble)

            val window = TimelineWindowCoordinator(settings).windowFor(address).value
            assertEquals(-4, window.pastDays)
            assertEquals(-120, window.notificationFadeSeconds)
            assertEquals(10, window.futureDays)
        } finally {
            root.toFile().deleteRecursively()
        }
    }

    @Test
    fun `preserves any existing timeline window group`() = runBlocking {
        val root = Files.createTempDirectory("legacy-rockpoold-timeline-")
        try {
            val watch = fakeWatch(connected = true) as KnownPebbleDevice
            val address = watch.identifier.asString.uppercase()
            val directory = root.resolve(address.replace(':', '_'))
            Files.createDirectories(directory)
            Files.writeString(
                directory.resolve("appsettings.conf"),
                """
                [timeline]
                pastDays=-4
                eventFadeout=-120
                futureDays=10
                """.trimIndent(),
            )
            val settings = RockpoolSettings(root.resolve("rockpool.properties"))
            settings.setChecked("${address.replace(':', '_')}.timeline.fade", "-900")
            val libPebble = object : LibPebble by FakeLibPebble() {
                override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))
            }

            LegacyRockpooldImporter(settings, root, readBondedAddresses = { emptySet() })
                .importIfNeeded(libPebble)

            assertEquals("-900", settings.get("${address.replace(':', '_')}.timeline.fade"))
            assertEquals("", settings.get("${address.replace(':', '_')}.timeline.start"))
            assertEquals("", settings.get("${address.replace(':', '_')}.timeline.end"))
            val window = TimelineWindowCoordinator(settings).windowFor(address).value
            assertEquals(-2, window.pastDays)
            assertEquals(-3600, window.notificationFadeSeconds)
            assertEquals(7, window.futureDays)
        } finally {
            root.toFile().deleteRecursively()
        }
    }
}
