/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.forms.submitForm
import io.ktor.client.statement.bodyAsChannel
import io.ktor.http.Parameters
import io.ktor.http.isSuccess
import io.ktor.utils.io.readRemaining
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.io.readByteArray
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import java.io.IOException

/** The daemon owns the saved session and keeps short-lived Firebase ID tokens in memory. */
internal class CoreStoreCredentials(
    private val settings: RockpoolSettings,
    private val refresh: suspend (String) -> CoreStoreRefresh = ::refreshCoreStoreToken,
    private val now: () -> Long = System::currentTimeMillis,
) {
    private val refreshMutex = Mutex()
    private val revision = MutableStateFlow(0L)
    val changes = revision.asStateFlow()
    private var cached: CachedToken? = null

    fun profile(): List<String> {
        val fields = settings.entries(PREFIX)
        return if (fields.isEmpty()) emptyList() else listOf(
            fields.getValue(UID), fields[NAME].orEmpty(), fields[EMAIL].orEmpty(),
        )
    }

    @Synchronized
    fun save(refreshToken: String, uid: String, name: String, email: String): Boolean {
        if (!validCredential(refreshToken) || !validCredential(uid, 128) || name.length > 256 || email.length > 320) {
            return false
        }
        return replace(mapOf(TOKEN to refreshToken, UID to uid, NAME to name, EMAIL to email))
    }

    @Synchronized
    fun clear(): Boolean = replace(emptyMap())

    private fun replace(fields: Map<String, String>): Boolean {
        if (!settings.replacePrefix(PREFIX, fields)) return false
        cached = null
        revision.value++
        return true
    }

    /** An account change invalidates this handle even when the same UID signs in again. */
    class Session(val uid: String, internal val generation: Long)

    @Synchronized
    fun currentSession(): Session? = settings.get(UID).takeIf { it.isNotEmpty() }
        ?.let { Session(it, revision.value) }

    @Synchronized
    fun <T> withCurrentSession(session: Session, action: () -> T): T? =
        if (session.generation == revision.value && session.uid == settings.get(UID)) action() else null

    suspend fun token(): String? = tokenFor(null)

    suspend fun token(session: Session): String? = tokenFor(session)

    private suspend fun tokenFor(session: Session?): String? = refreshMutex.withLock {
        val generation: Long
        val uid: String
        val refreshToken: String
        synchronized(this) {
            val fields = settings.entries(PREFIX)
            if (session != null && (session.generation != revision.value || session.uid != fields[UID])) {
                throw CoreStoreCredentialFailure("Core Devices account changed", authentication = true)
            }
            if (fields.isEmpty()) return@withLock null
            uid = fields[UID] ?: throw CoreStoreCredentialFailure("Invalid Core Devices account")
            refreshToken = fields[TOKEN] ?: throw CoreStoreCredentialFailure("Invalid Core Devices account")
            cached?.takeIf { now() < it.expiresAt }?.let { return@withLock it.idToken }
            generation = revision.value
        }
        val requestedAt = now()
        val result = try {
            refresh(refreshToken)
        } catch (e: CancellationException) {
            throw e
        } catch (_: Exception) {
            throw CoreStoreCredentialFailure("Could not refresh the Core Devices account")
        }
        synchronized(this) {
            if (generation != revision.value) {
                throw CoreStoreCredentialFailure("Core Devices account changed", authentication = true)
            }
            if (result !is CoreStoreRefresh.Valid || result.uid != uid) {
                if (!clear()) throw CoreStoreCredentialFailure("Could not clear the expired Core Devices account")
                throw CoreStoreCredentialFailure("Core Devices sign-in has expired", authentication = true)
            }
            if (refreshToken != result.refreshToken && !settings.setChecked(TOKEN, result.refreshToken)) {
                throw CoreStoreCredentialFailure("Could not save the refreshed Core Devices account")
            }
            cached = CachedToken(result.idToken, requestedAt + (result.expiresInSeconds - 120).coerceAtLeast(0) * 1000L)
            result.idToken
        }
    }

    private class CachedToken(val idToken: String, val expiresAt: Long)

    private companion object {
        const val PREFIX = "account.core."
        const val TOKEN = PREFIX + "refreshToken"
        const val UID = PREFIX + "uid"
        const val NAME = PREFIX + "displayName"
        const val EMAIL = PREFIX + "email"
    }
}

internal class CoreStoreCredentialFailure(message: String, val authentication: Boolean = false) : IOException(message)

internal sealed interface CoreStoreRefresh {
    data object Revoked : CoreStoreRefresh

    class Valid(val uid: String, val idToken: String, val refreshToken: String, val expiresInSeconds: Int) :
        CoreStoreRefresh
}

private const val MAX_RESPONSE_SIZE = 64 * 1024L

private fun jsonObject(value: String): JsonObject? = Json.parseToJsonElement(value) as? JsonObject
private fun JsonObject.string(key: String): String? = (get(key) as? JsonPrimitive)
    ?.takeIf { it.isString }?.contentOrNull

private fun JsonObject.credential(key: String, limit: Int = 16 * 1024): String? =
    string(key)?.takeIf { validCredential(it, limit) }

private fun validCredential(value: String, limit: Int = 16 * 1024): Boolean =
    value.isNotEmpty() && value.length <= limit && value.all { it.code in 0x21..0x7e }

private val refreshClient by lazy {
    HttpClient(CIO) {
        followRedirects = false
        install(HttpTimeout) {
            requestTimeoutMillis = 15_000
            connectTimeoutMillis = 5_000
            socketTimeoutMillis = 10_000
        }
    }
}

private suspend fun refreshCoreStoreToken(refreshToken: String): CoreStoreRefresh {
    // This is the public Firebase web API key used by the Pebble CLI and Rockpool login.
    val response = refreshClient.submitForm(
        "https://securetoken.googleapis.com/v1/token?key=AIzaSyBZ9Cdvwwv9At2lPmc8TxyyEqSXGXejGvc",
        Parameters.build {
            append("grant_type", "refresh_token")
            append("refresh_token", refreshToken)
        },
    )
    val bytes = response.bodyAsChannel().readRemaining(MAX_RESPONSE_SIZE + 1).readByteArray()
    val body = bytes.takeIf { it.size <= MAX_RESPONSE_SIZE }?.let { jsonObject(it.decodeToString()) }
        ?: throw CoreStoreCredentialFailure("Invalid Core Devices token response")
    if (!response.status.isSuccess()) {
        val code = (body["error"] as? JsonObject)?.string("message")
        if (response.status.value in 400..499 &&
            code in setOf("TOKEN_EXPIRED", "INVALID_REFRESH_TOKEN", "USER_DISABLED", "USER_NOT_FOUND")
        ) return CoreStoreRefresh.Revoked
        throw CoreStoreCredentialFailure("Core Devices token refresh failed")
    }
    val uid = body.credential("user_id", 128)
    val idToken = body.credential("id_token")
    val renewed = body.credential("refresh_token")
    val expires = (body["expires_in"] as? JsonPrimitive)?.contentOrNull?.toIntOrNull()?.takeIf { it in 1..86400 }
    if (uid == null || idToken == null || renewed == null || expires == null) {
        throw CoreStoreCredentialFailure("Invalid Core Devices token response")
    }
    return CoreStoreRefresh.Valid(uid, idToken, renewed, expires)
}
