/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.PebbleBleIdentifier
import java.lang.reflect.Proxy
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.fail

class RockpoolConnectRoutingTest {
    @Test
    fun `saved watch reconnects without discovery even with different address case`() = runBlocking {
        var connections = 0
        val watch = knownWatch { connections++ }

        connectRockpoolWatch("aa:bb:cc:dd:ee:ff", listOf(watch)) {
            fail("Saved watch must not depend on scan visibility")
        }

        assertEquals(1, connections)
    }

    @Test
    fun `another saved watch does not intercept candidate connection`() = runBlocking {
        val watch = knownWatch { fail("Wrong saved watch connected") }
        val requests = mutableListOf<String>()

        connectRockpoolWatch("11:22:33:44:55:66", listOf(watch)) { requests += it }

        assertEquals(listOf("11:22:33:44:55:66"), requests)
    }

    @Test
    fun `first pairing retains candidate rediscovery path`() = runBlocking {
        val requests = mutableListOf<String>()

        connectRockpoolWatch("AA:BB:CC:DD:EE:FF", emptyList()) { requests += it }

        assertEquals(listOf("AA:BB:CC:DD:EE:FF"), requests)
    }

    private fun knownWatch(connect: () -> Unit): KnownPebbleDevice = Proxy.newProxyInstance(
        KnownPebbleDevice::class.java.classLoader,
        arrayOf(KnownPebbleDevice::class.java),
    ) { _, method, _ ->
        when (method.name) {
            "getIdentifier" -> PebbleBleIdentifier("AA:BB:CC:DD:EE:FF")
            "connect" -> { connect(); null }
            else -> error("Unexpected watch method: ${method.name}")
        }
    } as KnownPebbleDevice
}
