/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.concurrent.CopyOnWriteArrayList

internal data class AccountSessionChange(
    val token: String,
    val generation: Long,
)

internal data class AccountIdentityChange(
    val name: String,
    val email: String,
    val generation: Long,
)

/** Owns the account credential record shared by the primary and compatibility APIs. */
internal class AccountSettingsCoordinator(
    private val currentToken: () -> String,
    private val persist: (Map<String, String>) -> Boolean,
    private val transitionAccount: suspend (() -> Boolean) -> Boolean = { commit -> commit() },
) {
    constructor(
        settings: RockpoolSettings,
        transitionAccount: suspend (() -> Boolean) -> Boolean = { commit -> commit() },
    ) : this(
        currentToken = { settings.get(TOKEN_KEY) },
        persist = settings::setAllChecked,
        transitionAccount = transitionAccount,
    )

    private val listeners = CopyOnWriteArrayList<(AccountSessionChange) -> Unit>()
    private val identityListeners = CopyOnWriteArrayList<(AccountIdentityChange) -> Unit>()
    private val tokenMutations = AccountTokenMutations()
    private val mutex = Mutex()
    private var generation = 0L

    fun enqueueTokenMutation(token: String): AccountTokenMutation = tokenMutations.enqueue(token)

    suspend fun setToken(token: String): Boolean = mutex.withLock {
        if (currentToken() == token) return true
        // Identity is authenticated data, not a user preference. Clear it in the same durable
        // update as a logout/account switch. The locker transition retires old cloud rows and
        // their timeline tokens before this callback publishes the new credential.
        val committed = transitionAccount {
            persist(
                mapOf(
                    TOKEN_KEY to token,
                    NAME_KEY to "",
                    EMAIL_KEY to "",
                ),
            )
        }
        if (!committed) {
            return false
        }
        generation += 1
        val change = AccountSessionChange(token, generation)
        listeners.forEach { it(change) }
        return true
    }

    suspend fun currentSession(): AccountSessionChange = mutex.withLock {
        AccountSessionChange(currentToken(), generation)
    }

    suspend fun setIdentity(
        session: AccountSessionChange,
        name: String,
        email: String,
    ): Boolean = mutex.withLock {
        if (
            session.token.isEmpty() ||
            session.generation != generation ||
            session.token != currentToken()
        ) {
            return false
        }
        if (!persist(mapOf(NAME_KEY to name, EMAIL_KEY to email))) {
            return false
        }
        val change = AccountIdentityChange(name, email, generation)
        identityListeners.forEach { it(change) }
        true
    }

    fun addListener(listener: (AccountSessionChange) -> Unit): AutoCloseable {
        listeners += listener
        return AutoCloseable { listeners -= listener }
    }

    fun addIdentityListener(listener: (AccountIdentityChange) -> Unit): AutoCloseable {
        identityListeners += listener
        return AutoCloseable { identityListeners -= listener }
    }

    private companion object {
        const val TOKEN_KEY = "account.oauthToken"
        const val NAME_KEY = "account.name"
        const val EMAIL_KEY = "account.email"
    }
}
