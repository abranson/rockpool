/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertSame
import kotlin.test.assertTrue

class RockworkScanCandidatePolicyTest {
    @Test
    fun `BLE wins when one address is reported over both transports`() {
        val classic = candidate("classic", RockworkScanTransport.CLASSIC)
        val ble = candidate("ble", RockworkScanTransport.BLE)

        val results = RockworkScanCandidatePolicy.merge(
            existing = mapOf(classic.normalizedAddress to classic),
            hiddenAddresses = emptySet(),
            discoveries = listOf(ble),
        )

        assertSame(ble, results.getValue(classic.normalizedAddress))
    }

    @Test
    fun `same transport advertisement replaces stale candidate`() {
        val stale = candidate("stale", RockworkScanTransport.BLE)
        val refreshed = candidate("refreshed", RockworkScanTransport.BLE)

        val results = RockworkScanCandidatePolicy.merge(
            existing = mapOf(stale.normalizedAddress to stale),
            hiddenAddresses = emptySet(),
            discoveries = listOf(refreshed),
        )

        assertSame(refreshed, results.getValue(stale.normalizedAddress))
    }

    @Test
    fun `hidden candidates are removed and ignored`() {
        val known = candidate("known", RockworkScanTransport.BLE)
        val discoveredAgain = candidate("discovered-again", RockworkScanTransport.CLASSIC)

        val results = RockworkScanCandidatePolicy.merge(
            existing = mapOf(known.normalizedAddress to known),
            hiddenAddresses = setOf(known.normalizedAddress),
            discoveries = listOf(discoveredAgain),
        )

        assertEquals(emptyMap(), results)
    }

    @Test
    fun `candidate matching keeps a remembered transport during rediscovery`() {
        val ble = candidate("ble", RockworkScanTransport.BLE)

        assertTrue(
            RockworkScanCandidatePolicy.matches(
                ble,
                normalizedAddress = ble.normalizedAddress,
                transport = RockworkScanTransport.BLE,
            )
        )
        assertFalse(
            RockworkScanCandidatePolicy.matches(
                ble,
                normalizedAddress = ble.normalizedAddress,
                transport = RockworkScanTransport.CLASSIC,
            )
        )
        assertFalse(
            RockworkScanCandidatePolicy.matches(
                ble,
                normalizedAddress = "11:22:33:44:55:66",
                transport = RockworkScanTransport.BLE,
            )
        )
    }

    private fun candidate(
        device: String,
        transport: RockworkScanTransport,
    ) = RockworkScanCandidate(
        normalizedAddress = "AA:BB:CC:DD:EE:FF",
        device = device,
        transport = transport,
    )
}
