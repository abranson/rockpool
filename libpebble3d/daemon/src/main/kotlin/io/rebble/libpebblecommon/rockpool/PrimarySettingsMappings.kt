/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.database.entity.HealthGender
import io.rebble.libpebblecommon.health.HealthSettings
import org.freedesktop.dbus.types.Variant

/** A primary-API canned-response group, in the order supplied by the caller. */
internal data class PrimaryCannedResponseGroup(
    val name: String,
    val values: List<String>,
)

internal const val PRIMARY_CANNED_PREFIX = "primary.messaging.canned."
internal const val PRIMARY_CANNED_CONFIGURED_SETTING =
    "${PRIMARY_CANNED_PREFIX}configured"

/**
 * Apply the primary API's normalized health settings to [existing].
 *
 * The primary API stores height in centimetres and weight in kilograms, while
 * libpebble3 uses millimetres and decagrams.  The explicit conversions avoid
 * silently wrapping the wire-format [Short] fields.
 */
internal fun mergePrimaryHealthSettings(
    existing: HealthSettings,
    settings: Map<String, String>,
): HealthSettings {
    val enabled = settings.primaryBoolean("health.enabled") ?: existing.trackingEnabled
    val age = settings.primaryInt("health.age", 1..130) ?: existing.ageYears
    val heightCm = settings.primaryInt("health.height", 50..300)
    val gender = settings.primaryInt("health.gender", 0..2)
    val weightKg = settings.primaryInt("health.weight", 10..500)
    val moreActive = settings.primaryBoolean("health.moreActive") ?: existing.activityInsightsEnabled
    val sleepMore = settings.primaryBoolean("health.sleepMore") ?: existing.sleepInsightsEnabled
    val imperial = settings.primaryBoolean("units.imperial") ?: existing.imperialUnits

    return existing.copy(
        heightMm = heightCm?.let { checkedShort("health.height", it.toLong() * 10) } ?: existing.heightMm,
        weightDag = weightKg?.let { checkedShort("health.weight", it.toLong() * 100) } ?: existing.weightDag,
        trackingEnabled = enabled,
        activityInsightsEnabled = moreActive,
        sleepInsightsEnabled = sleepMore,
        ageYears = age,
        gender = gender?.let { HealthGender.entries[it] } ?: existing.gender,
        imperialUnits = imperial,
    )
}

/** Project the one account-global libpebble3 row onto every primary Health1 object. */
internal fun primaryHealthSettings(settings: HealthSettings): Map<String, Variant<*>> = linkedMapOf(
    "enabled" to Variant(settings.trackingEnabled),
    "age" to Variant(settings.ageYears),
    "height" to Variant(settings.heightMm.toInt() / 10),
    "gender" to Variant(settings.gender.ordinal),
    "weight" to Variant(settings.weightDag.toInt() / 100),
    "moreActive" to Variant(settings.activityInsightsEnabled),
    "sleepMore" to Variant(settings.sleepInsightsEnabled),
    "imperialUnits" to Variant(settings.imperialUnits),
)

/** Flatten ordered primary canned-response records into libpebble3 config values. */
internal fun flattenPrimaryCannedResponses(
    groups: List<PrimaryCannedResponseGroup>,
): List<String> = groups
    .asSequence()
    .flatMap { it.values.asSequence() }
    .map(String::trim)
    .filter(String::isNotEmpty)
    .distinct()
    .toList()

/**
 * Decode normalized primary canned settings below [prefix].  Group IDs are
 * sorted to make the result stable even when the settings implementation does
 * not preserve map iteration order.
 */
internal fun flattenPrimaryCannedResponses(
    settings: Map<String, String>,
    prefix: String,
): List<String> {
    val groups = settings.keys
        .asSequence()
        .filter { it.startsWith(prefix) && it.endsWith(PRIMARY_CANNED_NAME_SUFFIX) }
        .sorted()
        .map { nameKey ->
            val id = nameKey.removePrefix(prefix).removeSuffix(PRIMARY_CANNED_NAME_SUFFIX)
            PrimaryCannedResponseGroup(
                name = settings.getValue(nameKey),
                values = settings["$prefix$id$PRIMARY_CANNED_VALUES_SUFFIX"]
                    .orEmpty()
                    .split(PRIMARY_CANNED_SEPARATOR)
                    .filter(String::isNotEmpty),
            )
        }
        .toList()
    return flattenPrimaryCannedResponses(groups)
}

/** Decode the durable global primary canned groups into their D-Bus record shape. */
internal fun primaryCannedResponseRecords(
    settings: Map<String, String>,
    prefix: String,
): List<Map<String, Variant<*>>> = settings.entries
    .asSequence()
    .filter { it.key.startsWith(prefix) && it.key.endsWith(PRIMARY_CANNED_NAME_SUFFIX) }
    .sortedBy { it.key }
    .map { (nameKey, name) ->
        val id = nameKey.removePrefix(prefix).removeSuffix(PRIMARY_CANNED_NAME_SUFFIX)
        val values = settings["$prefix$id$PRIMARY_CANNED_VALUES_SUFFIX"]
            .orEmpty()
            .split(PRIMARY_CANNED_SEPARATOR)
            .filter(String::isNotEmpty)
        linkedMapOf(
            "id" to Variant(id),
            "name" to Variant(name),
            "values" to Variant(values, "as"),
        )
    }
    .toList()

private fun Map<String, String>.primaryBoolean(key: String): Boolean? = this[key]?.let { value ->
    requireNotNull(value.toBooleanStrictOrNull()) { "invalid $key" }
}

private fun Map<String, String>.primaryInt(key: String, range: IntRange): Int? = this[key]?.let { value ->
    val parsed = value.toIntOrNull()
    require(parsed in range) { "invalid $key" }
    parsed
}

private fun checkedShort(key: String, value: Long): Short {
    require(value in Short.MIN_VALUE.toLong()..Short.MAX_VALUE.toLong()) { "invalid $key" }
    return value.toShort()
}

internal const val PRIMARY_CANNED_NAME_SUFFIX = ".name"
private const val PRIMARY_CANNED_VALUES_SUFFIX = ".values"
private const val PRIMARY_CANNED_SEPARATOR = "\u001f"
