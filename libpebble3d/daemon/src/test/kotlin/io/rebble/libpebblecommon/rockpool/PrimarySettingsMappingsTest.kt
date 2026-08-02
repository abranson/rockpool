/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.database.entity.HRMonitoringInterval
import io.rebble.libpebblecommon.database.entity.HealthGender
import io.rebble.libpebblecommon.health.HealthSettings
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class PrimarySettingsMappingsTest {
    @Test
    fun `primary health settings convert units and retain heart rate settings`() {
        val existing = healthSettings()

        val merged = mergePrimaryHealthSettings(
            existing,
            mapOf(
                "health.enabled" to "true",
                "health.age" to "41",
                "health.height" to "183",
                "health.gender" to "1",
                "health.weight" to "72",
                "health.moreActive" to "true",
                "health.sleepMore" to "false",
                "units.imperial" to "true",
            ),
        )

        assertEquals(1830, merged.heightMm.toInt())
        assertEquals(7200, merged.weightDag.toInt())
        assertEquals(true, merged.trackingEnabled)
        assertEquals(41, merged.ageYears)
        assertEquals(HealthGender.Male, merged.gender)
        assertEquals(true, merged.activityInsightsEnabled)
        assertEquals(false, merged.sleepInsightsEnabled)
        assertEquals(true, merged.imperialUnits)
        assertEquals(existing.hrmEnabled, merged.hrmEnabled)
        assertEquals(existing.hrmMeasurementInterval, merged.hrmMeasurementInterval)
        assertEquals(existing.hrmActivityTrackingEnabled, merged.hrmActivityTrackingEnabled)
        assertEquals(existing.restingHr, merged.restingHr)
        assertEquals(existing.elevatedHr, merged.elevatedHr)
        assertEquals(existing.maxHr, merged.maxHr)
        assertEquals(existing.hrZone1Threshold, merged.hrZone1Threshold)
        assertEquals(existing.hrZone2Threshold, merged.hrZone2Threshold)
        assertEquals(existing.hrZone3Threshold, merged.hrZone3Threshold)
    }

    @Test
    fun `primary health projection uses the canonical libpebble row`() {
        val settings = healthSettings().copy(
            trackingEnabled = true,
            ageYears = 42,
            heightMm = 1810,
            gender = HealthGender.Other,
            weightDag = 7350,
            activityInsightsEnabled = true,
            sleepInsightsEnabled = false,
            imperialUnits = true,
        )

        assertEquals(
            mapOf(
                "enabled" to true,
                "age" to 42,
                "height" to 181,
                "gender" to 2,
                "weight" to 73,
                "moreActive" to true,
                "sleepMore" to false,
                "imperialUnits" to true,
            ),
            primaryHealthSettings(settings).mapValues { it.value.value },
        )
    }

    @Test
    fun `primary health mapping rejects values that do not fit libpebble shorts`() {
        assertFailsWith<IllegalArgumentException> {
            mergePrimaryHealthSettings(healthSettings(), mapOf("health.weight" to "500"))
        }
    }

    @Test
    fun `ordered canned groups flatten trimmed unique values`() {
        val responses = flattenPrimaryCannedResponses(
            listOf(
                PrimaryCannedResponseGroup("Quick", listOf(" Yes ", "", "No")),
                PrimaryCannedResponseGroup("Later", listOf("Yes", " Call me ", "  ")),
            ),
        )

        assertEquals(listOf("Yes", "No", "Call me"), responses)
    }

    @Test
    fun `normalized canned settings use stable group ordering`() {
        val separator = "\u001f"
        val responses = flattenPrimaryCannedResponses(
            linkedMapOf(
                "watch.example.canned.z.name" to "Later",
                "watch.example.canned.z.values" to "Later$separator Yes ",
                "watch.example.canned.a.name" to "Quick",
                "watch.example.canned.a.values" to " Ok $separator$separator Yes ",
            ),
            "watch.example.canned.",
        )

        assertEquals(listOf("Ok", "Yes", "Later"), responses)
    }

    @Test
    fun `global primary canned records ignore obsolete per-watch groups`() {
        val separator = "\u001f"
        val records = primaryCannedResponseRecords(
            settings = mapOf(
                "primary.messaging.canned.later.name" to "Later",
                "primary.messaging.canned.later.values" to "Call me${separator}Soon",
                "primary.messaging.canned.quick.name" to "Quick",
                "primary.messaging.canned.quick.values" to "Yes${separator}No",
                "watch.old.canned.stale.name" to "Stale",
                "watch.old.canned.stale.values" to "Wrong",
            ),
            prefix = "primary.messaging.canned.",
        )

        assertEquals(listOf("later", "quick"), records.map { it.getValue("id").value })
        assertEquals(listOf("Later", "Quick"), records.map { it.getValue("name").value })
        assertEquals(
            listOf(listOf("Call me", "Soon"), listOf("Yes", "No")),
            records.map { it.getValue("values").value },
        )
    }

    private fun healthSettings() = HealthSettings(
        heightMm = 1700,
        weightDag = 6500,
        trackingEnabled = false,
        activityInsightsEnabled = false,
        sleepInsightsEnabled = true,
        ageYears = 30,
        gender = HealthGender.Female,
        imperialUnits = false,
        hrmEnabled = true,
        hrmMeasurementInterval = HRMonitoringInterval.ThirtyMin,
        hrmActivityTrackingEnabled = true,
        restingHr = 61,
        elevatedHr = 121,
        maxHr = 181,
        hrZone1Threshold = 91,
        hrZone2Threshold = 111,
        hrZone3Threshold = 141,
    )
}
