/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.LibPebbleConfig
import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.database.entity.HealthGender
import io.rebble.libpebblecommon.health.HealthSettings
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class LegacyGlobalSettingsReconcilerTest {
    @Test
    fun `single legacy calendar and health candidate initialize global state once`() = runBlocking {
        withSettings { settings ->
            settings.setChecked("watch.one.calendar.enabled", "false")
            setLegacyHealth(settings, "one", age = 42, gender = "male", imperial = true)
            settings.setAllChecked(
                mapOf(
                    "canned.keys" to "org.example.chat",
                    "canned.org.example.chat" to "Yes\u001fNo",
                ),
            )
            val base = FakeLibPebble().healthSettings.first()
            var config = LibPebbleConfig()
            var imported: HealthSettings? = null
            var initializeCalls = 0
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                initializeHealth = { transform ->
                    initializeCalls++
                    imported = transform(base)
                    HealthSettingsInitializationResult.Initialized
                },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertFalse(config.watchConfig.calendarPins)
            assertEquals("false", settings.get(PRIMARY_CALENDAR_ENABLED_SETTING))
            assertEquals("legacy-single", settings.get(PRIMARY_CALENDAR_PROVENANCE_SETTING))
            assertEquals(1, initializeCalls)
            assertEquals(
                base.copy(
                    heightMm = 1800,
                    weightDag = 8000,
                    trackingEnabled = true,
                    activityInsightsEnabled = true,
                    sleepInsightsEnabled = false,
                    ageYears = 42,
                    gender = HealthGender.Male,
                    imperialUnits = true,
                ),
                imported,
            )
            assertEquals(
                "preserved-source-scoped",
                settings.get("migration.rockpoold.global-settings.v1.canned"),
            )
            assertEquals(
                "imported-single",
                settings.get("migration.rockpoold.global-settings.v1.units"),
            )
            assertEquals("org.example.chat", settings.get("canned.keys"))
            assertEquals("Yes\u001fNo", settings.get("canned.org.example.chat"))
            assertEquals("complete", settings.get("migration.rockpoold.global-settings.v1"))

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertEquals(1, initializeCalls)
        }
    }

    @Test
    fun `conflicting legacy watches are retained without choosing either`() = runBlocking {
        withSettings { settings ->
            settings.setAllChecked(
                mapOf(
                    "watch.one.calendar.enabled" to "true",
                    "watch.two.calendar.enabled" to "false",
                ),
            )
            setLegacyHealth(settings, "one", age = 35)
            setLegacyHealth(settings, "two", age = 36)
            var config = LibPebbleConfig()
            var configUpdates = 0
            var initializeCalls = 0
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = {
                    configUpdates++
                    config = it
                },
                initializeHealth = { _ ->
                    initializeCalls++
                    HealthSettingsInitializationResult.Initialized
                },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertTrue(config.watchConfig.calendarPins)
            assertEquals(0, configUpdates)
            assertFalse(settings.contains(PRIMARY_CALENDAR_ENABLED_SETTING))
            assertEquals(1, initializeCalls)
            assertEquals(
                "conflict",
                settings.get("migration.rockpoold.global-settings.v1.calendar"),
            )
            assertEquals(
                "conflict",
                settings.get("migration.rockpoold.global-settings.v1.health"),
            )
            assertEquals(
                "imported-identical",
                settings.get("migration.rockpoold.global-settings.v1.units"),
            )
            assertEquals("35", settings.get("watch.one.health.age"))
            assertEquals("36", settings.get("watch.two.health.age"))
        }
    }

    @Test
    fun `current canonical calendar and persisted health always win`() = runBlocking {
        withSettings { settings ->
            settings.setAllChecked(
                mapOf(
                    PRIMARY_CALENDAR_ENABLED_SETTING to "true",
                    PRIMARY_CALENDAR_PROVENANCE_SETTING to
                        PRIMARY_CALENDAR_PROVENANCE_EXPLICIT,
                    "watch.one.calendar.enabled" to "false",
                ),
            )
            setLegacyHealth(settings, "one", age = 60)
            var config = LibPebbleConfig()
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                initializeHealth = { HealthSettingsInitializationResult.Existing },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertTrue(config.watchConfig.calendarPins)
            assertEquals(
                "preserved-current",
                settings.get("migration.rockpoold.global-settings.v1.calendar"),
            )
            assertEquals(
                "preserved-current",
                settings.get("migration.rockpoold.global-settings.v1.health"),
            )
        }
    }

    @Test
    fun `stored config not generated from defaults is authoritative`() = runBlocking {
        withSettings { settings ->
            settings.setChecked("watch.one.calendar.enabled", "true")
            var config = LibPebbleConfig().copy(
                watchConfig = LibPebbleConfig().watchConfig.copy(calendarPins = false),
            )
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                configGeneratedFromDefault = false,
                initializeHealth = { HealthSettingsInitializationResult.Existing },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertFalse(config.watchConfig.calendarPins)
            assertEquals("false", settings.get(PRIMARY_CALENDAR_ENABLED_SETTING))
            assertEquals("existing-config", settings.get(PRIMARY_CALENDAR_PROVENANCE_SETTING))
            assertEquals(
                "preserved-current",
                settings.get("migration.rockpoold.global-settings.v1.calendar"),
            )
        }
    }

    @Test
    fun `pending v1 and malformed candidates never mutate global state`() = runBlocking {
        withSettings { settings ->
            settings.setAllChecked(
                mapOf(
                    "watch.one.calendar.enabled" to "maybe",
                    "watch.one.health.enabled" to "true",
                ),
            )
            var config = LibPebbleConfig()
            var initializeCalls = 0
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                initializeHealth = { _ ->
                    initializeCalls++
                    HealthSettingsInitializationResult.Initialized
                },
            )

            assertFalse(reconciler.reconcileIfNeeded(legacyImportComplete = false))
            assertFalse(reconciler.isComplete())
            assertTrue(config.watchConfig.calendarPins)
            assertEquals(0, initializeCalls)

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertEquals(
                "invalid-source",
                settings.get("migration.rockpoold.global-settings.v1.calendar"),
            )
            assertEquals(
                "invalid-source",
                settings.get("migration.rockpoold.global-settings.v1.health"),
            )
            assertNull(settings.entries(PRIMARY_CALENDAR_PREFIX)[PRIMARY_CALENDAR_ENABLED_SETTING])
            assertEquals(0, initializeCalls)
        }
    }

    @Test
    fun `preexisting persisted default calendar value is authoritative`() = runBlocking {
        withSettings { settings ->
            settings.setChecked("watch.one.calendar.enabled", "false")
            var config = LibPebbleConfig()
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                configGeneratedFromDefault = false,
                initializeHealth = { HealthSettingsInitializationResult.Existing },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertTrue(config.watchConfig.calendarPins)
            assertEquals("true", settings.get(PRIMARY_CALENDAR_ENABLED_SETTING))
            assertEquals("existing-config", settings.get(PRIMARY_CALENDAR_PROVENANCE_SETTING))
            assertEquals(
                "preserved-current",
                settings.get("migration.rockpoold.global-settings.v1.calendar"),
            )
        }
    }

    @Test
    fun `generated default config remains eligible after a daemon restart`() = runBlocking {
        withSettings { settings ->
            var firstConfig = LibPebbleConfig()
            val first = reconciler(
                settings = settings,
                currentConfig = { firstConfig },
                updateConfig = { firstConfig = it },
                configGeneratedFromDefault = true,
                initializeHealth = { HealthSettingsInitializationResult.Existing },
            )
            assertFalse(first.reconcileIfNeeded(legacyImportComplete = false))

            settings.setChecked("watch.one.calendar.enabled", "false")
            var restartedConfig = LibPebbleConfig()
            val restarted = reconciler(
                settings = settings,
                currentConfig = { restartedConfig },
                updateConfig = { restartedConfig = it },
                configGeneratedFromDefault = true,
                initializeHealth = { HealthSettingsInitializationResult.Existing },
            )

            assertTrue(restarted.reconcileIfNeeded(legacyImportComplete = true))
            assertFalse(restartedConfig.watchConfig.calendarPins)
            assertEquals("legacy-single", settings.get(PRIMARY_CALENDAR_PROVENANCE_SETTING))
        }
    }

    @Test
    fun `units migrate independently and conflicting units are retained`() = runBlocking {
        withSettings { settings ->
            settings.setChecked("watch.one.units.imperial", "true")
            val base = FakeLibPebble().healthSettings.first()
            var imported: HealthSettings? = null
            var config = LibPebbleConfig()
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                initializeHealth = { transform ->
                    imported = transform(base)
                    HealthSettingsInitializationResult.Initialized
                },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertEquals(true, imported?.imperialUnits)
            assertEquals(
                "no-source",
                settings.get("migration.rockpoold.global-settings.v1.health"),
            )
            assertEquals(
                "imported-single",
                settings.get("migration.rockpoold.global-settings.v1.units"),
            )
        }

        withSettings { settings ->
            settings.setAllChecked(
                mapOf(
                    "watch.one.units.imperial" to "true",
                    "watch.two.units.imperial" to "false",
                ),
            )
            var initializeCalls = 0
            var config = LibPebbleConfig()
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                initializeHealth = { _ ->
                    initializeCalls++
                    HealthSettingsInitializationResult.Initialized
                },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertEquals(0, initializeCalls)
            assertEquals(
                "conflict",
                settings.get("migration.rockpoold.global-settings.v1.units"),
            )
        }
    }

    @Test
    fun `failed health initialization prevents a completed migration marker`() = runBlocking {
        withSettings { settings ->
            setLegacyHealth(settings, "one", age = 44)
            val base = FakeLibPebble().healthSettings.first()
            var failing = true
            var config = LibPebbleConfig()
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { config = it },
                initializeHealth = { transform ->
                    if (failing) {
                        error("database unavailable")
                    } else {
                        transform(base)
                        HealthSettingsInitializationResult.Initialized
                    }
                },
            )

            assertFalse(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertFalse(reconciler.isComplete())
            assertFalse(settings.contains("migration.rockpoold.global-settings.v1"))

            failing = false
            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertTrue(reconciler.isComplete())
        }
    }

    @Test
    fun `invalid canonical calendar state is terminal and never rewrites config`() = runBlocking {
        withSettings { settings ->
            settings.setAllChecked(
                mapOf(
                    PRIMARY_CALENDAR_ENABLED_SETTING to "invalid",
                    PRIMARY_CALENDAR_PROVENANCE_SETTING to
                        PRIMARY_CALENDAR_PROVENANCE_EXPLICIT,
                ),
            )
            val config = LibPebbleConfig()
            var configUpdates = 0
            val reconciler = reconciler(
                settings = settings,
                currentConfig = { config },
                updateConfig = { configUpdates++ },
                initializeHealth = { HealthSettingsInitializationResult.Existing },
            )

            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertTrue(reconciler.isComplete())
            assertEquals(
                "invalid-current",
                settings.get("migration.rockpoold.global-settings.v1.calendar"),
            )
            assertTrue(reconciler.reconcileIfNeeded(legacyImportComplete = true))
            assertEquals(0, configUpdates)
        }
    }

    private fun reconciler(
        settings: RockpoolSettings,
        currentConfig: () -> LibPebbleConfig,
        updateConfig: (LibPebbleConfig) -> Unit,
        configGeneratedFromDefault: Boolean = true,
        initializeHealth: suspend (
            (HealthSettings) -> HealthSettings,
        ) -> HealthSettingsInitializationResult,
    ) = LegacyGlobalSettingsReconciler(
        settings = settings,
        configMutations = LibPebbleConfigMutationCoordinator(currentConfig, updateConfig),
        configGeneratedFromDefault = configGeneratedFromDefault,
        initializeHealthSettingsIfAbsent = initializeHealth,
    )

    private fun setLegacyHealth(
        settings: RockpoolSettings,
        id: String,
        age: Int,
        gender: String = "female",
        imperial: Boolean = false,
    ) {
        val prefix = "watch.$id"
        assertTrue(
            settings.setAllChecked(
                mapOf(
                    "$prefix.health.enabled" to "true",
                    "$prefix.health.age" to age.toString(),
                    "$prefix.health.height" to "180",
                    "$prefix.health.gender" to gender,
                    "$prefix.health.weight" to "80",
                    "$prefix.health.moreActive" to "true",
                    "$prefix.health.sleepMore" to "false",
                    "$prefix.units.imperial" to imperial.toString(),
                ),
            ),
        )
    }

    private suspend fun withSettings(block: suspend (RockpoolSettings) -> Unit) {
        val directory = Files.createTempDirectory("legacy-global-settings-")
        try {
            block(RockpoolSettings(directory.resolve("rockpool.properties")))
        } finally {
            directory.toFile().deleteRecursively()
        }
    }
}
