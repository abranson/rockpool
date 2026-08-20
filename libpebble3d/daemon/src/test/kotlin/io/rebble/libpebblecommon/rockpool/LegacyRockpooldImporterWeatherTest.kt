/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.compat.rockwork.decodeRockworkWeatherSettings
import io.rebble.libpebblecommon.compat.rockwork.encodeRockworkWeatherSettings
import io.rebble.libpebblecommon.compat.rockwork.parseRockworkWeatherLocations
import io.rebble.libpebblecommon.connection.FakeLibPebble
import kotlinx.coroutines.runBlocking
import org.freedesktop.dbus.types.Variant
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LegacyRockpooldImporterWeatherTest {
    @Test
    fun `completion requires both the original and weather migration markers`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { emptySet() })

            assertFalse(importer.isComplete())
            assertFalse(importer.isOriginalImportComplete())
            assertFalse(importer.isWeatherMigrationComplete())

            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            assertFalse(importer.isComplete())
            assertTrue(importer.isOriginalImportComplete())
            assertFalse(importer.isWeatherMigrationComplete())

            assertTrue(settings.setChecked(WEATHER_MARKER, COMPLETE))
            assertTrue(importer.isComplete())
            assertTrue(importer.isWeatherMigrationComplete())
        }
    }

    @Test
    fun `imports one physical weather fixture after the original migration completed`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val address = "AA:BB:CC:DD:EE:01"
            writeWeather(
                root = root,
                address = address,
                values = """
                    [weatherApp]
                    size=1
                    1\name=M%C3%BCnchen%2BNord
                    1\lat=48.1351
                    1\lng=11.5820
                """.trimIndent(),
            )
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            val importer = LegacyRockpooldImporter(
                settings = settings,
                legacyRoot = root,
                readBondedAddresses = { setOf(address) },
            )

            importer.importIfNeeded(FakeLibPebble())

            val locations = decodeRockworkWeatherSettings(settings.entries(WEATHER_PREFIX)).getOrThrow()
            assertEquals(1, locations.size)
            assertEquals("München+Nord", locations.single().name)
            assertEquals("48.1351", locations.single().latitude)
            assertEquals("11.5820", locations.single().longitude)
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `identical physical weather locations in two directories import once`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val addresses = setOf("AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:02")
            addresses.forEach { address ->
                writeWeather(root, address, weatherFixture("London", "51.5074", "-0.1278"))
            }
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { addresses })

            importer.importIfNeeded(FakeLibPebble())

            val locations = decodeRockworkWeatherSettings(settings.entries(WEATHER_PREFIX)).getOrThrow()
            assertEquals(1, locations.size)
            assertEquals("London", locations.single().name)
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `conflicting physical weather locations complete without choosing either source`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val addresses = setOf("AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:02")
            writeWeather(root, "AA:BB:CC:DD:EE:01", weatherFixture("London", "51.5074", "-0.1278"))
            writeWeather(root, "AA:BB:CC:DD:EE:02", weatherFixture("Paris", "48.8566", "2.3522"))
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            val importer = LegacyRockpooldImporter(settings, root, readBondedAddresses = { addresses })

            importer.importIfNeeded(FakeLibPebble())

            assertTrue(settings.entries(WEATHER_PREFIX).isEmpty())
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `an explicit empty canonical weather collection wins over legacy locations`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val address = "AA:BB:CC:DD:EE:01"
            writeWeather(root, address, weatherFixture("London", "51.5074", "-0.1278"))
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            assertTrue(settings.setChecked("${WEATHER_PREFIX}count", "0"))
            val importer = LegacyRockpooldImporter(
                settings,
                root,
                readBondedAddresses = { setOf(address) },
            )

            importer.importIfNeeded(FakeLibPebble())

            assertEquals(emptyList(), decodeRockworkWeatherSettings(settings.entries(WEATHER_PREFIX)).getOrThrow())
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `an unmatched weather directory defers then imports after its watch is eligible`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val address = "AA:BB:CC:DD:EE:01"
            writeWeather(root, address, weatherFixture("London", "51.5074", "-0.1278"))
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            var addresses = emptySet<String>()
            val importer = LegacyRockpooldImporter(
                settings,
                root,
                readBondedAddresses = { addresses },
            )

            importer.importIfNeeded(FakeLibPebble())

            assertFalse(settings.contains(WEATHER_MARKER))
            assertFalse(importer.isComplete())
            assertTrue(importer.isOriginalImportComplete())

            addresses = setOf(address)
            importer.importIfNeeded(FakeLibPebble())

            assertEquals(
                "London",
                decodeRockworkWeatherSettings(settings.entries(WEATHER_PREFIX)).getOrThrow().single().name,
            )
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `an oversized physical weather array is rejected without allocation`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val address = "AA:BB:CC:DD:EE:01"
            writeWeather(
                root,
                address,
                """
                    [weatherApp]
                    size=2147483647
                """.trimIndent(),
            )
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            val importer = LegacyRockpooldImporter(
                settings,
                root,
                readBondedAddresses = { setOf(address) },
            )

            importer.importIfNeeded(FakeLibPebble())

            assertTrue(settings.entries(WEATHER_PREFIX).isEmpty())
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `an unreadable weather source defers then retries after repair`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val address = "AA:BB:CC:DD:EE:01"
            val source = root.resolve(address.replace(':', '_')).resolve("appsettings.conf")
            Files.createDirectories(source)
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            val importer = LegacyRockpooldImporter(
                settings,
                root,
                readBondedAddresses = { setOf(address) },
            )

            importer.importIfNeeded(FakeLibPebble())

            assertFalse(settings.contains(WEATHER_MARKER))
            assertFalse(importer.isComplete())

            Files.delete(source)
            Files.writeString(source, weatherFixture("London", "51.5074", "-0.1278"))
            importer.importIfNeeded(FakeLibPebble())

            assertEquals(
                "London",
                decodeRockworkWeatherSettings(settings.entries(WEATHER_PREFIX)).getOrThrow().single().name,
            )
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    @Test
    fun `a persisted canonical weather target without its marker only finalizes`() = runBlocking {
        withDirectories { root, settingsRoot ->
            val address = "AA:BB:CC:DD:EE:01"
            writeWeather(root, address, weatherFixture("Paris", "48.8566", "2.3522"))
            val settings = RockpoolSettings(settingsRoot.resolve("rockpool.properties"))
            assertTrue(settings.setChecked(OLD_MARKER, COMPLETE))
            assertTrue(settings.setAllChecked(canonicalLocation("London", "51.5074", "-0.1278")))
            val importer = LegacyRockpooldImporter(
                settings,
                root,
                readBondedAddresses = { setOf(address) },
            )

            importer.importIfNeeded(FakeLibPebble())

            val locations = decodeRockworkWeatherSettings(settings.entries(WEATHER_PREFIX)).getOrThrow()
            assertEquals("London", locations.single().name)
            assertEquals(COMPLETE, settings.get(WEATHER_MARKER))
            assertTrue(importer.isComplete())
        }
    }

    private fun weatherFixture(name: String, latitude: String, longitude: String): String = """
        [weatherApp]
        size=1
        1\name=$name
        1\lat=$latitude
        1\lng=$longitude
    """.trimIndent()

    private fun canonicalLocation(
        name: String,
        latitude: String,
        longitude: String,
    ): Map<String, String> = encodeRockworkWeatherSettings(
        parseRockworkWeatherLocations(
            listOf(Variant(listOf(name, latitude, longitude), "as")),
        ),
    )

    private fun writeWeather(root: Path, address: String, values: String) {
        val directory = root.resolve(address.replace(':', '_'))
        Files.createDirectories(directory)
        Files.writeString(directory.resolve("appsettings.conf"), values)
    }

    private suspend fun withDirectories(block: suspend (Path, Path) -> Unit) {
        val root = Files.createTempDirectory("legacy-rockpoold-weather-")
        val settingsRoot = Files.createTempDirectory("rockpool-settings-")
        try {
            block(root, settingsRoot)
        } finally {
            root.toFile().deleteRecursively()
            settingsRoot.toFile().deleteRecursively()
        }
    }

    private companion object {
        const val COMPLETE = "complete"
        const val OLD_MARKER = "migration.rockpoold.v1"
        const val WEATHER_MARKER = "migration.rockpoold.weather-locations.v1"
        const val WEATHER_PREFIX = "weather.locations."
    }
}
