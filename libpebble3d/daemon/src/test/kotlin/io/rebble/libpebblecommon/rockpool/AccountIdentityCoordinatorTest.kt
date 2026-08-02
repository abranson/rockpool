/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.linux.web.RebbleAccountIdentity
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class AccountIdentityCoordinatorTest {
    @Test
    fun `startup refresh persists identity for the current session`() = runBlocking {
        var token = "account-a"
        val state = mutableMapOf<String, String>()
        val testScope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val accountSettings = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                state += values
                values["account.oauthToken"]?.let { token = it }
                true
            },
        )
        val coordinator = AccountIdentityCoordinator(
            accountSettings = accountSettings,
            fetchIdentity = { RebbleAccountIdentity("Account A", "a@example.test") },
            scope = testScope,
        )

        try {
            coordinator.start()

            assertEquals("Account A", state["account.name"])
            assertEquals("a@example.test", state["account.email"])
        } finally {
            testScope.cancel()
        }
    }

    @Test
    fun `account switch cancels the old refresh and accepts the replacement`() = runBlocking {
        var token = "account-a"
        val state = mutableMapOf<String, String>()
        val testScope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val firstStarted = CompletableDeferred<Unit>()
        val firstCancelled = CompletableDeferred<Unit>()
        var fetchCount = 0
        val accountSettings = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                state += values
                values["account.oauthToken"]?.let { token = it }
                true
            },
        )
        val coordinator = AccountIdentityCoordinator(
            accountSettings = accountSettings,
            fetchIdentity = {
                fetchCount += 1
                if (fetchCount == 1) {
                    firstStarted.complete(Unit)
                    try {
                        awaitCancellation()
                    } finally {
                        firstCancelled.complete(Unit)
                    }
                }
                RebbleAccountIdentity("Account B", null)
            },
            scope = testScope,
        )

        try {
            coordinator.start()
            firstStarted.await()
            assertTrue(accountSettings.setToken("account-b"))

            firstCancelled.await()
            assertEquals("Account B", state["account.name"])
            assertEquals("", state["account.email"])
            assertEquals(2, fetchCount)
        } finally {
            testScope.cancel()
        }
    }
}
