/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class SailfishPairingRequesterTest {
    @Test
    fun `pairing continues once the provider accepts dispatch`() {
        val dispatched = mutableListOf<String>()
        val provider = PlatformProviderController(
            phoneNativeAvailable = { true },
            pairingNative = {
                dispatched += it
                0
            },
        )
        val requester = SailfishPairingRequester(provider::requestPairing)

        assertTrue(requester.requestPairing(ADDRESS))
        assertEquals(listOf(ADDRESS), dispatched)
    }

    @Test
    fun `pairing cannot continue when the provider rejects dispatch`() {
        val provider = PlatformProviderController(
            phoneNativeAvailable = { true },
            pairingNative = { -1 },
        )

        assertFalse(SailfishPairingRequester(provider::requestPairing).requestPairing(ADDRESS))
    }

    @Test
    fun `an invalid address never reaches the provider`() {
        var dispatched = false
        val provider = PlatformProviderController(
            phoneNativeAvailable = { true },
            pairingNative = {
                dispatched = true
                0
            },
        )

        assertFalse(SailfishPairingRequester(provider::requestPairing).requestPairing("invalid"))
        assertFalse(dispatched)
    }

    private companion object {
        const val ADDRESS = "AA:BB:CC:DD:EE:FF"
    }
}
