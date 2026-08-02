/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class AccountLockerUpgradeTest {
    @Test
    fun `retired and complete markers prevent a repeated destructive transition`() {
        assertFalse(isAccountLockerSessionGateRetired(""))
        assertFalse(isAccountLockerSessionGateRetired("invalid"))
        assertTrue(isAccountLockerSessionGateRetired("retired"))
        assertTrue(isAccountLockerSessionGateRetired("complete"))
    }

    @Test
    fun `existing marker skips account transition`() = runBlocking {
        var transitions = 0

        val result = initializeAccountLockerSessionGate(
            isInitialized = { true },
            markRetired = { error("marker must not be rewritten") },
            transitionAccount = {
                transitions += 1
                it()
            },
        )

        assertEquals(AccountLockerUpgradeResult.ALREADY_INITIALIZED, result)
        assertEquals(0, transitions)
    }

    @Test
    fun `first upgrade marks only from inside retirement transition`() = runBlocking {
        val events = mutableListOf<String>()

        val result = initializeAccountLockerSessionGate(
            isInitialized = { false },
            markRetired = {
                events += "marker"
                true
            },
            transitionAccount = { mark ->
                events += "retire"
                val marked = mark()
                events += "transition-complete"
                marked
            },
        )

        assertEquals(AccountLockerUpgradeResult.INITIALIZED, result)
        assertEquals(listOf("retire", "marker", "transition-complete"), events)
    }

    @Test
    fun `failed durable marker leaves upgrade pending`() = runBlocking {
        var transitions = 0

        val result = initializeAccountLockerSessionGate(
            isInitialized = { false },
            markRetired = { false },
            transitionAccount = { mark ->
                transitions += 1
                mark()
            },
        )

        assertEquals(AccountLockerUpgradeResult.FAILED, result)
        assertEquals(1, transitions)
    }
}
