/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class AccountSettingsCoordinatorTest {
    @Test
    fun `primary and compatibility admissions share one invocation-order queue`() = runBlocking {
        var token = "original-token"
        val writes = mutableListOf<String>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                token = values.getValue("account.oauthToken")
                writes += token
                true
            },
        )
        val primary = coordinator.enqueueTokenMutation("temporary-token")
        val compatibility = coordinator.enqueueTokenMutation("original-token")
        val compatibilityEntered = CompletableDeferred<Unit>()

        val compatibilityResult = async {
            compatibility.execute(beginCommit = { true }) { value ->
                compatibilityEntered.complete(Unit)
                coordinator.setToken(value)
            }
        }
        yield()
        assertFalse(compatibilityEntered.isCompleted)

        val primaryResult = async {
            primary.execute(beginCommit = { true }, persist = coordinator::setToken)
        }

        assertEquals(AccountTokenMutationResult.Saved, primaryResult.await())
        assertEquals(AccountTokenMutationResult.Saved, compatibilityResult.await())
        assertEquals(listOf("temporary-token", "original-token"), writes)
        assertEquals("original-token", token)
    }

    @Test
    fun `account switch clears identity in the same durable update`() = runBlocking {
        var token = "account-a"
        val writes = mutableListOf<Map<String, String>>()
        val changes = mutableListOf<AccountSessionChange>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                writes += values
                token = values.getValue("account.oauthToken")
                true
            },
        )
        coordinator.addListener(changes::add)

        assertTrue(coordinator.setToken("account-b"))
        assertEquals(
            mapOf(
                "account.oauthToken" to "account-b",
                "account.name" to "",
                "account.email" to "",
            ),
            writes.single(),
        )
        assertEquals(listOf(AccountSessionChange("account-b", 1)), changes)

        assertTrue(coordinator.setToken("account-b"))
        assertEquals(1, writes.size)
        assertEquals(1, changes.size)
    }

    @Test
    fun `failed account update retains old session and emits nothing`() = runBlocking {
        val changes = mutableListOf<AccountSessionChange>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { "account-a" },
            persist = { false },
        )
        coordinator.addListener(changes::add)

        assertFalse(coordinator.setToken("account-b"))
        assertEquals(emptyList(), changes)
    }

    @Test
    fun `retirement transition runs before persistence and publication`() = runBlocking {
        var token = "account-a"
        val events = mutableListOf<String>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                events += "persist"
                token = values.getValue("account.oauthToken")
                true
            },
            transitionAccount = { commit ->
                events += "transition"
                commit()
            },
        )
        coordinator.addListener { events += "listener" }

        assertTrue(coordinator.setToken("account-b"))
        assertEquals(listOf("transition", "persist", "listener"), events)
    }

    @Test
    fun `logout clears identity and does not notify removed listeners`() = runBlocking {
        var token = "account-a"
        val writes = mutableListOf<Map<String, String>>()
        val changes = mutableListOf<AccountSessionChange>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                writes += values
                token = values.getValue("account.oauthToken")
                true
            },
        )
        val removedListener = coordinator.addListener { error("removed listener was invoked") }
        coordinator.addListener(changes::add)
        removedListener.close()

        assertTrue(coordinator.setToken(""))
        assertEquals(
            mapOf(
                "account.oauthToken" to "",
                "account.name" to "",
                "account.email" to "",
            ),
            writes.single(),
        )
        assertEquals(listOf(AccountSessionChange("", 1)), changes)
    }

    @Test
    fun `identity is persisted and published only for its current session`() = runBlocking {
        var token = "account-a"
        val writes = mutableListOf<Map<String, String>>()
        val identities = mutableListOf<AccountIdentityChange>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { token },
            persist = { values ->
                writes += values
                values["account.oauthToken"]?.let { token = it }
                true
            },
        )
        coordinator.addIdentityListener(identities::add)
        val accountA = coordinator.currentSession()

        assertTrue(coordinator.setIdentity(accountA, "Account A", "a@example.test"))
        assertEquals(
            mapOf("account.name" to "Account A", "account.email" to "a@example.test"),
            writes.single(),
        )
        assertEquals(
            listOf(AccountIdentityChange("Account A", "a@example.test", 0)),
            identities,
        )

        assertTrue(coordinator.setToken("account-b"))
        assertFalse(coordinator.setIdentity(accountA, "Stale A", "stale@example.test"))
        assertEquals(2, writes.size)
        assertEquals(1, identities.size)
    }

    @Test
    fun `failed identity persistence emits nothing`() = runBlocking {
        val identities = mutableListOf<AccountIdentityChange>()
        val coordinator = AccountSettingsCoordinator(
            currentToken = { "account-a" },
            persist = { false },
        )
        coordinator.addIdentityListener(identities::add)

        assertFalse(
            coordinator.setIdentity(
                coordinator.currentSession(),
                "Account A",
                "a@example.test",
            ),
        )
        assertTrue(identities.isEmpty())
    }
}
