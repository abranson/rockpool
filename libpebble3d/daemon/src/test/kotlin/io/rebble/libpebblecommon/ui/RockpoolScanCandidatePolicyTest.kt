/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertSame
import kotlin.test.assertTrue

class RockpoolScanCandidatePolicyTest {
    @Test
    fun `BLE wins when one address is reported over both transports`() {
        val classic = candidate("classic", RockpoolScanTransport.CLASSIC)
        val ble = candidate("ble", RockpoolScanTransport.BLE)

        val results = RockpoolScanCandidatePolicy.merge(
            existing = mapOf(classic.normalizedAddress to classic),
            hiddenAddresses = emptySet(),
            discoveries = listOf(ble),
        )

        assertSame(ble, results.getValue(classic.normalizedAddress))
    }

    @Test
    fun `same transport advertisement replaces stale candidate`() {
        val stale = candidate("stale", RockpoolScanTransport.BLE)
        val refreshed = candidate("refreshed", RockpoolScanTransport.BLE)

        val results = RockpoolScanCandidatePolicy.merge(
            existing = mapOf(stale.normalizedAddress to stale),
            hiddenAddresses = emptySet(),
            discoveries = listOf(refreshed),
        )

        assertSame(refreshed, results.getValue(stale.normalizedAddress))
    }

    @Test
    fun `hidden candidates are removed and ignored`() {
        val known = candidate("known", RockpoolScanTransport.BLE)
        val discoveredAgain = candidate("discovered-again", RockpoolScanTransport.CLASSIC)

        val results = RockpoolScanCandidatePolicy.merge(
            existing = mapOf(known.normalizedAddress to known),
            hiddenAddresses = setOf(known.normalizedAddress),
            discoveries = listOf(discoveredAgain),
        )

        assertEquals(emptyMap(), results)
    }

    @Test
    fun `candidate matching keeps a remembered transport during rediscovery`() {
        val ble = candidate("ble", RockpoolScanTransport.BLE)

        assertTrue(
            RockpoolScanCandidatePolicy.matches(
                ble,
                normalizedAddress = ble.normalizedAddress,
                transport = RockpoolScanTransport.BLE,
            )
        )
        assertFalse(
            RockpoolScanCandidatePolicy.matches(
                ble,
                normalizedAddress = ble.normalizedAddress,
                transport = RockpoolScanTransport.CLASSIC,
            )
        )
        assertFalse(
            RockpoolScanCandidatePolicy.matches(
                ble,
                normalizedAddress = "11:22:33:44:55:66",
                transport = RockpoolScanTransport.BLE,
            )
        )
    }

    private fun candidate(
        device: String,
        transport: RockpoolScanTransport,
    ) = RockpoolScanCandidate(
        normalizedAddress = "AA:BB:CC:DD:EE:FF",
        device = device,
        transport = transport,
    )
}
