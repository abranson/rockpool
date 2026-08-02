/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.LibPebbleConfig
import io.rebble.libpebblecommon.database.entity.HealthGender
import io.rebble.libpebblecommon.health.HealthSettings

internal const val PRIMARY_CALENDAR_PREFIX = "primary.timeline.calendar."
internal const val PRIMARY_CALENDAR_ENABLED_SETTING = "${PRIMARY_CALENDAR_PREFIX}enabled"
internal const val PRIMARY_CALENDAR_PROVENANCE_SETTING = "${PRIMARY_CALENDAR_PREFIX}provenance"
internal const val PRIMARY_CALENDAR_PROVENANCE_EXPLICIT = "explicit"

/**
 * Reconciles account-global settings only after the per-watch legacy import is complete.
 *
 * Calendar and health used to be stored once per watch. They are imported only when every
 * complete candidate agrees, and never overwrite a current canonical record. Legacy canned
 * groups deliberately remain source-scoped in the compatibility store: flattening them into
 * libpebble3's generic response list would leak one application's replies into another.
 */
internal class LegacyGlobalSettingsReconciler(
    private val settings: RockpoolSettings,
    private val configMutations: LibPebbleConfigMutationCoordinator,
    private val configGeneratedFromDefault: Boolean,
    private val initializeHealthSettingsIfAbsent: suspend (
        (HealthSettings) -> HealthSettings,
    ) -> HealthSettingsInitializationResult,
) {
    private val logger = Logger.withTag("LegacyGlobalSettings")
    @Volatile
    private var projectionReady = false

    fun isComplete(): Boolean = settings.get(MARKER) == COMPLETE && projectionReady

    suspend fun reconcileIfNeeded(legacyImportComplete: Boolean): Boolean {
        if (!legacyImportComplete) return false
        if (settings.get(MARKER) == COMPLETE) {
            projectionReady = projectCanonicalCalendar()
            return projectionReady
        }

        val calendarOutcome = reconcileCalendar() ?: return false
        val health = reconcileHealth() ?: return false
        val completed = settings.setAllChecked(
            mapOf(
                CALENDAR_OUTCOME to calendarOutcome,
                HEALTH_OUTCOME to health.healthOutcome,
                UNITS_OUTCOME to health.unitsOutcome,
                CANNED_OUTCOME to CANNED_SOURCE_SCOPED,
                MARKER to COMPLETE,
            ),
        )
        if (!completed) {
            logger.w { "global legacy-settings migration outcome was not persisted; will retry" }
        } else {
            projectionReady = projectCanonicalCalendar()
        }
        return completed && projectionReady
    }

    /** Reapply a persisted canonical value after a crash between the two durable stores. */
    private fun projectCanonicalCalendar(): Boolean {
        var valid = true
        return runCatching {
            configMutations.mutate { config ->
                val canonical = settings.entries(PRIMARY_CALENDAR_PREFIX)
                val enabled = canonical[PRIMARY_CALENDAR_ENABLED_SETTING]
                    ?.let(::parseBoolean)
                if (canonical.isNotEmpty() && enabled == null) {
                    valid = false
                    config
                } else if (enabled == null) {
                    config
                } else {
                    config.withCalendarEnabled(enabled)
                }
            }
            if (!valid) {
                logger.w {
                    "canonical calendar migration state is invalid; preserving config permanently"
                }
            }
            // Invalid state is a terminal preserved outcome, not a reason to rewrite forever.
            true
        }.getOrElse {
            logger.w(it) { "could not project canonical calendar migration state" }
            false
        }
    }

    private fun reconcileCalendar(): String? {
        val candidates = legacyCalendarCandidates()
        var outcome: String? = null
        var settingsSaved = true
        return runCatching {
            configMutations.mutate { config ->
                var selected: Boolean? = null
                settingsSaved = settings.updatePrefixChecked(PRIMARY_CALENDAR_PREFIX) { current ->
                    val currentValue = current[PRIMARY_CALENDAR_ENABLED_SETTING]
                        ?.let(::parseBoolean)
                    when {
                        current.isNotEmpty() && currentValue == null -> {
                            outcome = INVALID_CURRENT
                            current
                        }
                        currentValue != null -> {
                            selected = currentValue
                            outcome = PRESERVED_CURRENT
                            current
                        }
                        // Any older durable libpebble3 config is authoritative, including an
                        // explicitly persisted value equal to the compiled default. A default
                        // created by this migration remains unclaimed across daemon restarts.
                        !configGeneratedFromDefault -> {
                            selected = config.watchConfig.calendarPins
                            outcome = PRESERVED_CURRENT
                            canonicalCalendar(
                                config.watchConfig.calendarPins,
                                PROVENANCE_EXISTING_CONFIG,
                            )
                        }
                        candidates.value != null -> {
                            selected = candidates.value
                            outcome = candidates.outcome
                            canonicalCalendar(
                                candidates.value,
                                if (candidates.outcome == IMPORTED_SINGLE) {
                                    PROVENANCE_LEGACY_SINGLE
                                } else {
                                    PROVENANCE_LEGACY_IDENTICAL
                                },
                            )
                        }
                        else -> {
                            outcome = candidates.outcome
                            current
                        }
                    }
                }
                if (settingsSaved && selected != null) {
                    config.withCalendarEnabled(checkNotNull(selected))
                } else {
                    config
                }
            }
            if (!settingsSaved) null else outcome
        }.getOrElse {
            logger.w(it) { "calendar migration failed; will retry" }
            null
        }
    }

    private suspend fun reconcileHealth(): HealthReconciliation? {
        val health = legacyHealthCandidates()
        val units = legacyUnitsCandidates()
        if (health.value == null && units.value == null) {
            return HealthReconciliation(health.outcome, units.outcome)
        }
        return runCatching {
            when (
                initializeHealthSettingsIfAbsent { current ->
                    val withHealth = health.value?.applyTo(current) ?: current
                    units.value?.let { withHealth.copy(imperialUnits = it) } ?: withHealth
                }
            ) {
                HealthSettingsInitializationResult.Initialized -> HealthReconciliation(
                    healthOutcome = health.outcome,
                    unitsOutcome = units.outcome,
                )
                HealthSettingsInitializationResult.Existing -> HealthReconciliation(
                    healthOutcome = if (health.value == null) health.outcome else PRESERVED_CURRENT,
                    unitsOutcome = if (units.value == null) units.outcome else PRESERVED_CURRENT,
                )
            }
        }.getOrElse {
            logger.w(it) { "health settings migration failed; will retry" }
            null
        }
    }

    private fun legacyCalendarCandidates(): CandidateResolution<Boolean> {
        val raw = settings.entries("watch.")
            .filterKeys { it.endsWith(CALENDAR_SUFFIX) }
            .values
            .toList()
        if (raw.isEmpty()) return CandidateResolution(null, NO_SOURCE)
        val parsed = raw.map(::parseBoolean)
        if (parsed.any { it == null }) return CandidateResolution(null, INVALID_SOURCE)
        val values = parsed.filterNotNull().distinct()
        if (values.size != 1) return CandidateResolution(null, CONFLICT)
        return CandidateResolution(
            values.single(),
            if (raw.size == 1) IMPORTED_SINGLE else IMPORTED_IDENTICAL,
        )
    }

    private fun legacyHealthCandidates(): CandidateResolution<LegacyHealthSettings> {
        val values = settings.entries("watch.")
        val groups = linkedMapOf<String, MutableMap<String, String>>()
        values.forEach { (key, value) ->
            val health = HEALTH_SETTING.matchEntire(key)
            if (health != null) {
                groups.getOrPut(health.groupValues[1]) { linkedMapOf() }[
                    health.groupValues[2]
                ] = value
            }
        }
        val healthGroups = groups.values.toList()
        if (healthGroups.isEmpty()) return CandidateResolution(null, NO_SOURCE)

        val decoded = healthGroups.map(::decodeLegacyHealth)
        if (decoded.any { it == null }) return CandidateResolution(null, INVALID_SOURCE)
        val candidates = decoded.filterNotNull().distinct()
        if (candidates.size != 1) return CandidateResolution(null, CONFLICT)
        return CandidateResolution(
            candidates.single(),
            if (healthGroups.size == 1) IMPORTED_SINGLE else IMPORTED_IDENTICAL,
        )
    }

    private fun legacyUnitsCandidates(): CandidateResolution<Boolean> {
        val raw = settings.entries("watch.")
            .filterKeys { UNITS_SETTING.matches(it) }
            .values
            .toList()
        if (raw.isEmpty()) return CandidateResolution(null, NO_SOURCE)
        val parsed = raw.map(::parseBoolean)
        if (parsed.any { it == null }) return CandidateResolution(null, INVALID_SOURCE)
        val values = parsed.filterNotNull().distinct()
        if (values.size != 1) return CandidateResolution(null, CONFLICT)
        return CandidateResolution(
            values.single(),
            if (raw.size == 1) IMPORTED_SINGLE else IMPORTED_IDENTICAL,
        )
    }

    private fun decodeLegacyHealth(
        values: Map<String, String>,
    ): LegacyHealthSettings? = runCatching {
        if (!values.keys.containsAll(HEALTH_FIELDS)) return null
        val enabled = requireNotNull(parseBoolean(values.getValue(ENABLED)))
        val moreActive = requireNotNull(parseBoolean(values.getValue(MORE_ACTIVE)))
        val sleepMore = requireNotNull(parseBoolean(values.getValue(SLEEP_MORE)))
        val age = values.getValue(AGE).toInt().also { require(it in 1..130) }
        val height = values.getValue(HEIGHT).toInt().also { require(it in 50..300) }
        val weight = values.getValue(WEIGHT).toInt().also { require(it in 10..327) }
        val gender = when (values.getValue(GENDER).lowercase()) {
            "female" -> HealthGender.Female
            "male" -> HealthGender.Male
            else -> error("invalid legacy gender")
        }
        LegacyHealthSettings(
            enabled = enabled,
            age = age,
            heightCm = height,
            gender = gender,
            weightKg = weight,
            moreActive = moreActive,
            sleepMore = sleepMore,
        )
    }.getOrNull()

    private data class LegacyHealthSettings(
        val enabled: Boolean,
        val age: Int,
        val heightCm: Int,
        val gender: HealthGender,
        val weightKg: Int,
        val moreActive: Boolean,
        val sleepMore: Boolean,
    ) {
        fun applyTo(current: HealthSettings): HealthSettings = current.copy(
            heightMm = (heightCm * 10).toShort(),
            weightDag = (weightKg * 100).toShort(),
            trackingEnabled = enabled,
            activityInsightsEnabled = moreActive,
            sleepInsightsEnabled = sleepMore,
            ageYears = age,
            gender = gender,
        )
    }

    private data class HealthReconciliation(
        val healthOutcome: String,
        val unitsOutcome: String,
    )

    private fun canonicalCalendar(enabled: Boolean, provenance: String): Map<String, String> =
        mapOf(
            PRIMARY_CALENDAR_ENABLED_SETTING to enabled.toString(),
            PRIMARY_CALENDAR_PROVENANCE_SETTING to provenance,
        )

    private fun LibPebbleConfig.withCalendarEnabled(enabled: Boolean): LibPebbleConfig =
        if (watchConfig.calendarPins == enabled) this else copy(
            watchConfig = watchConfig.copy(calendarPins = enabled),
        )

    private data class CandidateResolution<T>(
        val value: T?,
        val outcome: String,
    )

    private companion object {
        const val MARKER = "migration.rockpoold.global-settings.v1"
        const val COMPLETE = "complete"
        const val CALENDAR_OUTCOME = "$MARKER.calendar"
        const val HEALTH_OUTCOME = "$MARKER.health"
        const val UNITS_OUTCOME = "$MARKER.units"
        const val CANNED_OUTCOME = "$MARKER.canned"
        const val CANNED_SOURCE_SCOPED = "preserved-source-scoped"

        const val IMPORTED_SINGLE = "imported-single"
        const val IMPORTED_IDENTICAL = "imported-identical"
        const val PRESERVED_CURRENT = "preserved-current"
        const val INVALID_CURRENT = "invalid-current"
        const val CONFLICT = "conflict"
        const val INVALID_SOURCE = "invalid-source"
        const val NO_SOURCE = "no-source"

        const val PROVENANCE_EXISTING_CONFIG = "existing-config"
        const val PROVENANCE_LEGACY_SINGLE = "legacy-single"
        const val PROVENANCE_LEGACY_IDENTICAL = "legacy-identical"

        const val CALENDAR_SUFFIX = ".calendar.enabled"
        const val IMPERIAL = "imperial"
        const val ENABLED = "enabled"
        const val AGE = "age"
        const val HEIGHT = "height"
        const val GENDER = "gender"
        const val WEIGHT = "weight"
        const val MORE_ACTIVE = "moreActive"
        const val SLEEP_MORE = "sleepMore"
        val HEALTH_FIELDS = setOf(ENABLED, AGE, HEIGHT, GENDER, WEIGHT, MORE_ACTIVE, SLEEP_MORE)
        val HEALTH_SETTING = Regex(
            "^watch\\.([^.]+)\\.health\\.(enabled|age|height|gender|weight|moreActive|sleepMore)$",
        )
        val UNITS_SETTING = Regex("^watch\\.([^.]+)\\.units\\.imperial$")

        fun parseBoolean(value: String): Boolean? = when (value.lowercase()) {
            "true", "1" -> true
            "false", "0" -> false
            else -> null
        }
    }
}
