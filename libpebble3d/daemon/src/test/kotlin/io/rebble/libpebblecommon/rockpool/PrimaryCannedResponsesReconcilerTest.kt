/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.LibPebbleConfig
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PrimaryCannedResponsesReconcilerTest {
    @Test
    fun `oversized replacement is rejected before commit or canonical persistence`() = withSettings {
        var config = LibPebbleConfig().let { current ->
            current.copy(watchConfig = current.watchConfig.copy(calendarPins = false))
        }
        var updates = 0
        var commits = 0
        val acceptedResponses = List(15) { index ->
            "$index:".padEnd(511, 'x')
        }
        val oversizedResponses = acceptedResponses.dropLast(1) +
            (acceptedResponses.last() + "z")
        val reconciler = reconciler(
            settings = it,
            current = { config },
            configFitsStorage = { projected ->
                // The concrete production seam's exact JSON/Preferences boundary is covered by
                // LibPebbleConfigHolderJvmTest. This test isolates the reconciler's atomic branch.
                projected.notificationConfig.cannedResponses != oversizedResponses
            },
            update = { next ->
                updates++
                config = next
            },
        )
        val acceptedValues = canonical(acceptedResponses)
        val oversizedCanonical = canonical(oversizedResponses)

        assertEquals(
            PrimaryCannedResponsesUpdateResult.Completed,
            reconciler.replace(acceptedValues, acceptedResponses) {
                commits++
                true
            },
        )
        val acceptedConfig = config
        val persistedCanonical = it.entries(PRIMARY_CANNED_PREFIX)

        val result = reconciler.replace(oversizedCanonical, oversizedResponses) {
            commits++
            true
        }

        assertEquals(PrimaryCannedResponsesUpdateResult.InvalidArgument, result)
        assertEquals(1, commits)
        assertEquals(1, updates)
        assertEquals(persistedCanonical, it.entries(PRIMARY_CANNED_PREFIX))
        assertEquals(acceptedConfig, config)
        assertEquals("true", persistedCanonical[PRIMARY_CANNED_CONFIGURED_SETTING])
    }

    @Test
    fun `oversized persisted canonical state is terminal and a valid replacement recovers`() =
        withSettings {
            val oversizedCanonical = canonical(listOf("preserved oversized response")) +
                (PRIMARY_CANNED_CONFIGURED_SETTING to "true")
            assertTrue(it.setAllChecked(oversizedCanonical))
            var config = LibPebbleConfig().let { current ->
                current.copy(
                    notificationConfig = current.notificationConfig.copy(
                        cannedResponses = listOf("Still active"),
                    ),
                )
            }
            val activeConfig = config
            var updates = 0
            var fits = false
            val reconciler = reconciler(
                settings = it,
                current = { config },
                configFitsStorage = { fits },
                update = { next ->
                    updates++
                    config = next
                },
            )

            assertTrue(reconciler.reconcile())
            assertTrue(reconciler.isComplete())
            assertEquals(0, updates)
            assertEquals(activeConfig, config)
            assertEquals(oversizedCanonical, it.entries(PRIMARY_CANNED_PREFIX))
            assertTrue(reconciler.reconcile())
            assertEquals(0, updates)
            assertEquals(activeConfig, config)
            assertEquals(oversizedCanonical, it.entries(PRIMARY_CANNED_PREFIX))

            fits = true
            val validResponses = listOf("Recovered")
            val validCanonical = canonical(validResponses)
            assertEquals(
                PrimaryCannedResponsesUpdateResult.Completed,
                reconciler.replace(validCanonical, validResponses) { true },
            )
            assertEquals(1, updates)
            assertEquals(validResponses, config.notificationConfig.cannedResponses)
            assertEquals(
                validCanonical + (PRIMARY_CANNED_CONFIGURED_SETTING to "true"),
                it.entries(PRIMARY_CANNED_PREFIX),
            )
        }

    @Test
    fun `replacement commits canonical marker and projected values together`() = withSettings {
        var config = LibPebbleConfig()
        var commits = 0
        val reconciler = reconciler(it, { config }) { config = it }
        val canonical = mapOf(
            "${PRIMARY_CANNED_PREFIX}one.name" to "One",
            "${PRIMARY_CANNED_PREFIX}one.values" to "Later",
        )

        val result = reconciler.replace(canonical, listOf("Later")) {
            commits++
            true
        }

        assertEquals(PrimaryCannedResponsesUpdateResult.Completed, result)
        assertEquals(1, commits)
        assertEquals("true", it.get(PRIMARY_CANNED_CONFIGURED_SETTING))
        assertEquals("One", it.get("${PRIMARY_CANNED_PREFIX}one.name"))
        assertEquals(listOf("Later"), config.notificationConfig.cannedResponses)
        assertTrue(reconciler.isComplete())
    }

    @Test
    fun `canonical groups replay into config without changing unrelated fields`() = withSettings {
        it.setAllChecked(
            mapOf(
                PRIMARY_CANNED_CONFIGURED_SETTING to "true",
                "${PRIMARY_CANNED_PREFIX}a.name" to "First",
                "${PRIMARY_CANNED_PREFIX}a.values" to " Yes \u001fNo",
                "${PRIMARY_CANNED_PREFIX}b.name" to "Second",
                "${PRIMARY_CANNED_PREFIX}b.values" to "Yes\u001fLater",
            ),
        )
        var config = LibPebbleConfig().copy(
            watchConfig = LibPebbleConfig().watchConfig.copy(calendarPins = false),
        )
        val reconciler = reconciler(it, { config }) { config = it }

        assertTrue(reconciler.reconcile())
        assertEquals(listOf("Yes", "No", "Later"), config.notificationConfig.cannedResponses)
        assertFalse(config.watchConfig.calendarPins)
    }

    @Test
    fun `marked empty canonical collection clears generated defaults`() = withSettings {
        it.setChecked(PRIMARY_CANNED_CONFIGURED_SETTING, "true")
        var config = LibPebbleConfig()
        val reconciler = reconciler(it, { config }) { config = it }

        assertTrue(reconciler.reconcile())
        assertEquals(emptyList(), config.notificationConfig.cannedResponses)
    }

    @Test
    fun `unmarked canonical groups from an earlier daemon are replayed`() = withSettings {
        it.setAllChecked(
            mapOf(
                "${PRIMARY_CANNED_PREFIX}legacy.name" to "Legacy",
                "${PRIMARY_CANNED_PREFIX}legacy.values" to "One\u001fTwo",
            ),
        )
        var config = LibPebbleConfig()
        val reconciler = reconciler(it, { config }) { config = it }

        assertTrue(reconciler.reconcile())
        assertEquals(listOf("One", "Two"), config.notificationConfig.cannedResponses)
    }

    @Test
    fun `absent canonical collection is a no-op`() = withSettings {
        var config = LibPebbleConfig()
        var updates = 0
        val reconciler = reconciler(it, { config }) { next ->
            updates++
            config = next
        }

        assertTrue(reconciler.reconcile())
        assertTrue(reconciler.isComplete())
        assertEquals(0, updates)
    }

    @Test
    fun `failed projection remains pending and a retry converges`() = withSettings {
        it.setAllChecked(
            mapOf(
                PRIMARY_CANNED_CONFIGURED_SETTING to "true",
                "${PRIMARY_CANNED_PREFIX}one.name" to "One",
                "${PRIMARY_CANNED_PREFIX}one.values" to "Retry",
            ),
        )
        var config = LibPebbleConfig()
        var fail = true
        val reconciler = reconciler(it, { config }) { next ->
            if (fail) error("storage unavailable")
            config = next
        }

        assertFalse(reconciler.reconcile())
        assertFalse(reconciler.isComplete())
        fail = false
        assertTrue(reconciler.reconcile())
        assertEquals(listOf("Retry"), config.notificationConfig.cannedResponses)
    }

    private fun reconciler(
        settings: RockpoolSettings,
        current: () -> LibPebbleConfig,
        configFitsStorage: (LibPebbleConfig) -> Boolean = { true },
        update: (LibPebbleConfig) -> Unit,
    ) = PrimaryCannedResponsesReconciler(
        settings,
        LibPebbleConfigMutationCoordinator(current, update),
        configFitsStorage,
    )

    private fun withSettings(test: (RockpoolSettings) -> Unit) {
        val directory = Files.createTempDirectory("primary-canned-reconciler-")
        try {
            test(RockpoolSettings(directory.resolve("rockpool.properties")))
        } finally {
            directory.toFile().deleteRecursively()
        }
    }

    private fun canonical(responses: List<String>): Map<String, String> = mapOf(
        "${PRIMARY_CANNED_PREFIX}group.name" to "Group",
        "${PRIMARY_CANNED_PREFIX}group.values" to responses.joinToString("\u001f"),
    )

}
