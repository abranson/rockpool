/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class PrimaryDiscoveryMappingsTest {
    @Test
    fun `cancelled scan mutation has no side effect and committed mutation runs once`() {
        var mutations = 0

        assertFalse(
            runCommittedScanMutation(beginCommit = { false }) { mutations += 1 },
        )
        assertEquals(0, mutations)

        assertTrue(
            runCommittedScanMutation(beginCommit = { true }) { mutations += 1 },
        )
        assertEquals(1, mutations)
    }

    @Test
    fun `scanning combines BLE and Classic state`() {
        assertFalse(primaryScanning(ble = false, classic = false))
        assertTrue(primaryScanning(ble = true, classic = false))
        assertTrue(primaryScanning(ble = false, classic = true))
        assertTrue(primaryScanning(ble = true, classic = true))
    }

    @Test
    fun `Classic capability follows runtime availability`() {
        assertEquals(
            listOf("transport.ble", "discovery.bond-import"),
            primaryManagerCapabilities(classicAvailable = false, concurrentWatches = false),
        )
        assertEquals(
            listOf(
                "transport.ble",
                "discovery.bond-import",
                "transport.classic",
                "watch.concurrent",
            ),
            primaryManagerCapabilities(classicAvailable = true, concurrentWatches = true),
        )
    }

    @Test
    fun `any scan retains BLE behavior and explicit Classic is gated`() {
        assertEquals(PrimaryDiscoveryTransport.BLE, primaryScanTransport(null, false))
        assertEquals(PrimaryDiscoveryTransport.BLE, primaryScanTransport("any", true))
        assertEquals(PrimaryDiscoveryTransport.BLE, primaryScanTransport("ble", true))
        assertNull(primaryScanTransport("classic", false))
        assertEquals(PrimaryDiscoveryTransport.CLASSIC, primaryScanTransport("classic", true))
        assertNull(primaryScanTransport("invalid", true))
    }

    @Test
    fun `pair transport support follows runtime Classic availability`() {
        assertTrue(primaryPairTransportSupported("any", false))
        assertTrue(primaryPairTransportSupported("ble", false))
        assertFalse(primaryPairTransportSupported("classic", false))
        assertTrue(primaryPairTransportSupported("classic", true))
        assertFalse(primaryPairTransportSupported("invalid", true))
    }

    @Test
    fun `explicit pair transport must match candidate transport`() {
        assertTrue(primaryPairTransportMatches("ble", "ble", classicAvailable = true))
        assertFalse(primaryPairTransportMatches("ble", "classic", classicAvailable = true))
        assertTrue(primaryPairTransportMatches("classic", "classic", classicAvailable = true))
        assertFalse(primaryPairTransportMatches("classic", "ble", classicAvailable = true))
        assertTrue(primaryPairTransportMatches("any", "ble", classicAvailable = true))
        assertTrue(primaryPairTransportMatches("any", "classic", classicAvailable = true))
        assertFalse(primaryPairTransportMatches("any", "classic", classicAvailable = false))
    }
}
