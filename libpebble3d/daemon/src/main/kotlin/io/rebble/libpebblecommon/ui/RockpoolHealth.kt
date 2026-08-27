/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.database.entity.HealthGender
import io.rebble.libpebblecommon.health.HealthSettings
import io.rebble.libpebblecommon.rockpool.HealthSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.HealthSettingsUpdate
import org.freedesktop.dbus.types.UInt16
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant

private val ROCKPOOL_HEALTH_KEYS = setOf(
    "enabled",
    "age",
    "gender",
    "height",
    "weight",
    "moreActive",
    "sleepMore",
)

internal typealias RockpoolHealthUpdate = HealthSettingsUpdate
internal typealias RockpoolHealthCoordinator = HealthSettingsCoordinator

/**
 * A per-exported-watch guard for the legacy enable-health sync request.
 *
 * [System.nanoTime] is monotonic, unlike wall-clock time, so a clock adjustment cannot cause a
 * burst of health requests.  The old Rockpool backend applied the same one-minute limit.
 */
internal class RockpoolHealthSyncThrottle(
    private val nowNanos: () -> Long = System::nanoTime,
) {
    private var lastRequestNanos: Long? = null

    @Synchronized
    fun tryAcquire(): Boolean {
        val now = nowNanos()
        val previous = lastRequestNanos
        if (previous != null && now - previous in 0 until HEALTH_SYNC_THROTTLE_NANOS) {
            return false
        }
        lastRequestNanos = now
        return true
    }

    private companion object {
        const val HEALTH_SYNC_THROTTLE_NANOS = 60_000_000_000L
    }
}

/** Convert the legacy QVariant map into the normalized keys used by libpebble3. */
internal fun normalizeRockpoolHealthParams(
    params: Map<String, Variant<*>>,
): Map<String, String> {
    require(params.isNotEmpty()) { "health settings cannot be empty" }
    require(params.keys.all { it in ROCKPOOL_HEALTH_KEYS }) { "unknown health setting" }

    return buildMap {
        params.forEach { (key, variant) ->
            val value = when (key) {
                "enabled", "moreActive", "sleepMore" ->
                    variant.rockpoolBoolean()?.toString()

                "age" -> variant.rockpoolInt()?.takeIf { it in 1..130 }?.toString()
                "height" -> variant.rockpoolInt()?.takeIf { it in 50..300 }?.toString()
                // libpebble3 stores decagrams in a signed short, hence 327 kg is the exact limit.
                "weight" -> variant.rockpoolInt()?.takeIf { it in 10..327 }?.toString()
                "gender" -> variant.rockpoolGender()?.toString()
                else -> null
            }
            requireNotNull(value) { "invalid health setting $key" }
            put("health.$key", value)
        }
    }
}

/** Reproduce the old org.rockpool record shape from libpebble3's actual current settings. */
internal fun rockpoolHealthParams(settings: HealthSettings): Map<String, Variant<*>> = linkedMapOf(
    "enabled" to Variant(settings.trackingEnabled),
    "age" to Variant(settings.ageYears),
    "gender" to Variant(if (settings.gender == HealthGender.Female) "female" else "male"),
    "height" to Variant(settings.heightMm.toInt() / 10),
    "weight" to Variant(settings.weightDag.toInt() / 100),
    "moreActive" to Variant(settings.activityInsightsEnabled),
    "sleepMore" to Variant(settings.sleepInsightsEnabled),
)

private fun Variant<*>.rockpoolBoolean(): Boolean? =
    if (sig == "b") value as? Boolean else null

private fun Variant<*>.rockpoolGender(): Int? = when {
    sig == "s" -> when ((value as? String)?.lowercase()) {
        "female" -> HealthGender.Female.ordinal
        "male" -> HealthGender.Male.ordinal
        else -> null
    }

    // The compatibility record has only female/male strings and cannot round-trip Other.
    else -> rockpoolInt()?.takeIf {
        it == HealthGender.Female.ordinal || it == HealthGender.Male.ordinal
    }
}

private fun Variant<*>.rockpoolInt(): Int? = when (sig) {
    "y" -> (value as? Byte)?.toUByte()?.toInt()
    "n" -> (value as? Short)?.toInt()
    "q" -> (value as? UInt16)?.toInt()
    "i" -> value as? Int
    "u" -> (value as? UInt32)?.toLong()?.takeIf { it <= Int.MAX_VALUE }?.toInt()
    // The compatibility QML sends age/height/weight from TextField.text.
    "s" -> (value as? String)?.toIntOrNull()
    else -> null
}
