/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.LibPebbleConfig
import java.util.prefs.Preferences
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class JvmConfigStoragePolicyTest {
    @Test
    fun `preferences capacity is inclusive`() {
        val exact = JvmConfigStoragePolicy(
            serializedLength = { Preferences.MAX_VALUE_LENGTH },
        )
        val oversized = JvmConfigStoragePolicy(
            serializedLength = { Preferences.MAX_VALUE_LENGTH + 1 },
        )

        assertTrue(exact.canPersist(LibPebbleConfig()))
        assertFalse(oversized.canPersist(LibPebbleConfig()))
    }

    @Test
    fun `canned admission reserves every later daemon config variant`() {
        val observed = mutableListOf<LibPebbleConfig>()
        val accepting = JvmConfigStoragePolicy(
            serializedLength = { config ->
                observed += config
                Preferences.MAX_VALUE_LENGTH
            },
        )

        assertTrue(accepting.canAdmitCannedResponses(LibPebbleConfig()))
        assertEquals(4, observed.size)
        assertTrue(
            observed.all {
                it.watchConfig.multipleConnectedWatchesSupported &&
                    it.watchConfig.lanDevConnection
            },
        )
        assertTrue(observed.any { !it.watchConfig.calendarPins })
        assertTrue(observed.any { it.bleConfig.verbosePpogLogging })

        val rejecting = JvmConfigStoragePolicy(
            serializedLength = { config ->
                if (!config.watchConfig.calendarPins && config.bleConfig.verbosePpogLogging) {
                    Preferences.MAX_VALUE_LENGTH + 1
                } else {
                    Preferences.MAX_VALUE_LENGTH
                }
            },
        )
        assertFalse(rejecting.canAdmitCannedResponses(LibPebbleConfig()))
    }

    @Test
    fun `mandatory daemon policy rejection does not write`() {
        var current = LibPebbleConfig()
        var writes = 0
        val mutations = LibPebbleConfigMutationCoordinator(
            current = { current },
            update = {
                current = it
                writes += 1
            },
        )
        val rejecting = JvmConfigStoragePolicy(
            serializedLength = { config ->
                if (
                    config.watchConfig.multipleConnectedWatchesSupported &&
                    config.watchConfig.lanDevConnection
                ) {
                    Preferences.MAX_VALUE_LENGTH + 1
                } else {
                    Preferences.MAX_VALUE_LENGTH
                }
            },
        )

        assertFalse(applyMandatoryDaemonConfigPolicy(mutations, rejecting))
        assertEquals(0, writes)
        assertFalse(current.watchConfig.multipleConnectedWatchesSupported)
        assertFalse(current.watchConfig.lanDevConnection)

        val accepting = JvmConfigStoragePolicy(
            serializedLength = { Preferences.MAX_VALUE_LENGTH },
        )
        assertTrue(applyMandatoryDaemonConfigPolicy(mutations, accepting))
        assertEquals(1, writes)
        assertTrue(current.watchConfig.multipleConnectedWatchesSupported)
        assertTrue(current.watchConfig.lanDevConnection)
    }
}
