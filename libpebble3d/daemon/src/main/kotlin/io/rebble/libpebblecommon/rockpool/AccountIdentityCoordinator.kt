/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.linux.web.RebbleAccountIdentity
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/** Fetches best-effort account display details without letting stale requests cross sessions. */
internal class AccountIdentityCoordinator(
    private val accountSettings: AccountSettingsCoordinator,
    private val fetchIdentity: suspend () -> RebbleAccountIdentity?,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
) {
    private val logger = Logger.withTag("AccountIdentity")
    private val lock = Any()
    private var requestedSession: AccountSessionChange? = null
    private var active: Job? = null

    init {
        accountSettings.addListener(::accountChanged)
    }

    fun start() {
        scope.launch {
            accountChanged(accountSettings.currentSession())
        }
    }

    private fun accountChanged(session: AccountSessionChange) {
        val next = if (session.token.isEmpty()) {
            null
        } else {
            scope.launch(start = CoroutineStart.LAZY) {
                try {
                    val identity = fetchIdentity() ?: return@launch
                    accountSettings.setIdentity(
                        session = session,
                        name = identity.name,
                        email = identity.email.orEmpty(),
                    )
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w { "account identity refresh failed" }
                }
            }
        }
        val previous = synchronized(lock) {
            if (requestedSession == session) {
                next?.cancel()
                return
            }
            requestedSession = session
            active.also { active = next }
        }
        previous?.cancel()
        next?.start()
    }
}
