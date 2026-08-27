/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.database.entity.SEND_TEXT_CANNED_SOURCE
import io.rebble.libpebblecommon.database.entity.SEND_TEXT_MAX_METHODS
import io.rebble.libpebblecommon.messaging.SendMessageResult
import io.rebble.libpebblecommon.messaging.SendTextFavorite
import io.rebble.libpebblecommon.messaging.SendTextRoute
import io.rebble.libpebblecommon.messaging.SystemMessaging
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.nio.ByteBuffer
import java.security.MessageDigest
import kotlin.uuid.Uuid

internal class SendTextConfigurationCoordinator(
    private val replaceConfiguration: suspend (List<SendTextFavorite>, List<String>) -> Unit,
    private val systemMessaging: SystemMessaging,
    private val settings: RockpoolSettings,
    private val scope: CoroutineScope,
) {
    private val logger = Logger.withTag("SendTextConfiguration")
    private val mutex = Mutex()
    private val listeners = mutableListOf<() -> Unit>()

    fun addListener(listener: () -> Unit) {
        synchronized(listeners) { listeners += listener }
    }

    fun requestReconcile(favoritesChanged: Boolean = false) {
        if (favoritesChanged) notifyListeners()
        scope.launch { reconcile() }
    }

    suspend fun reconcile(): Boolean = mutex.withLock {
        val configuration = runCatching {
            parseFavorites(readStoredList("contacts")) to
                readStoredList("canned")[SEND_TEXT_CANNED_SOURCE].orEmpty()
        }
            .getOrElse {
                logger.w { "preserving Send Text projection: ${it.message}" }
                return@withLock false
            }
        val (favorites, responses) = configuration
        if (!validResponses(responses)) {
            logger.w { "preserving Send Text projection: canned responses are invalid" }
            return@withLock false
        }
        runCatching { replaceConfiguration(favorites, responses) }
            .onFailure { logger.w { "Send Text reconciliation failed: ${it.message}" } }
            .isSuccess
    }

    fun favorites(): List<SendTextFavorite> = runCatching {
        parseFavorites(readStoredList("contacts"))
    }.getOrDefault(emptyList())

    suspend fun replaceFavorites(
        values: Map<String, List<String>>,
        beginCommit: () -> Boolean,
    ): SendTextFavoritesUpdateResult = mutex.withLock {
        val favorites = try {
            parseFavorites(values)
        } catch (_: IllegalArgumentException) {
            return@withLock SendTextFavoritesUpdateResult.InvalidArgument
        }
        val responses = try {
            readStoredList("canned")[SEND_TEXT_CANNED_SOURCE].orEmpty()
                .also { require(validResponses(it)) }
        } catch (_: IllegalArgumentException) {
            return@withLock SendTextFavoritesUpdateResult.InvalidArgument
        }
        if (!beginCommit()) return@withLock SendTextFavoritesUpdateResult.Cancelled
        if (!settings.replacePrefix(CONTACTS_PREFIX, encodeStoredList(CONTACTS_PREFIX, values))) {
            return@withLock SendTextFavoritesUpdateResult.PersistenceFailed
        }
        notifyListeners()
        if (runCatching { replaceConfiguration(favorites, responses) }.isFailure) {
            return@withLock SendTextFavoritesUpdateResult.ActivationFailed
        }
        SendTextFavoritesUpdateResult.Completed
    }

    suspend fun sendText(methodId: Uuid, text: String): SendTextDispatchResult = mutex.withLock {
        if (!validOutgoingText(text)) return@withLock SendTextDispatchResult.InvalidArgument
        val routes = runCatching { parseFavorites(readStoredList("contacts")) }
            .getOrElse { return@withLock SendTextDispatchResult.UnknownMethod }
            .flatMap(SendTextFavorite::routes)
            .filter { it.methodId == methodId }
        if (routes.size != 1) return@withLock SendTextDispatchResult.UnknownMethod
        val route = routes.single()
        when (runCatching {
            systemMessaging.sendMessage(route.accountId, route.recipient, text)
        }.getOrElse {
            logger.w { "Send Text dispatch failed: ${it.message}" }
            SendMessageResult.Failed
        }) {
            SendMessageResult.Sent -> SendTextDispatchResult.Sent
            SendMessageResult.Unavailable -> SendTextDispatchResult.Unavailable
            SendMessageResult.Failed -> SendTextDispatchResult.Failed
        }
    }

    fun validateFavorites(values: Map<String, List<String>>) {
        parseFavorites(values)
    }

    fun validateResponses(values: List<String>) {
        require(validResponses(values)) { "Send Text canned responses exceed firmware limits" }
    }

    private fun notifyListeners() {
        synchronized(listeners) { listeners.toList() }.forEach { listener ->
            runCatching(listener).onFailure {
                logger.w { "Send Text property listener failed: ${it.message}" }
            }
        }
    }

    private fun parseFavorites(values: Map<String, List<String>>): List<SendTextFavorite> {
        require(values.values.sumOf(List<String>::size) <= SEND_TEXT_MAX_METHODS)
        val displays = mutableSetOf<String>()
        return values.map { (name, encodedRoutes) ->
            require(name.isNotBlank() && name.encodeToByteArray().size <= 64)
            require(encodedRoutes.isNotEmpty())
            SendTextFavorite(name, encodedRoutes.map { encoded ->
                val separator = encoded.indexOf(':')
                require(separator > 0 && separator < encoded.lastIndex)
                val account = encoded.substring(0, separator)
                val recipient = encoded.substring(separator + 1)
                require(account.startsWith(TELEPATHY_ACCOUNT_PREFIX))
                require(account.encodeToByteArray().size <= 512)
                require(recipient.encodeToByteArray().size <= 64)
                require('\u0000' !in account && '\u0000' !in recipient)
                require(displays.add(recipient))
                SendTextRoute(
                    accountId = account,
                    recipient = recipient,
                    displayRecipient = recipient,
                    methodId = rockpoolSendTextMethodUuid("$account:$recipient"),
                )
            }, contactId = rockpoolSendTextContactUuid(name))
        }
    }

    private fun validResponses(values: List<String>): Boolean =
        values.size <= UByte.MAX_VALUE.toInt() &&
            values.all { it.isNotBlank() && '\u0000' !in it && it.encodeToByteArray().size <= 512 } &&
            values.joinToString("\u0000").encodeToByteArray().size <= 512

    private fun readStoredList(kind: String): Map<String, List<String>> {
        val prefix = "$kind."
        val stored = settings.entries(prefix)
        val names = stored["${prefix}keys"].orEmpty()
            .split(STORED_LIST_SEPARATOR)
            .filter(String::isNotEmpty)
        require(names.distinct().size == names.size)
        return names.associateWith { name ->
            require(name.isNotBlank() && STORED_LIST_SEPARATOR !in name)
            val encoded = requireNotNull(stored["$prefix$name"])
            encoded.split(STORED_LIST_SEPARATOR).filter(String::isNotEmpty)
        }
    }

    private fun encodeStoredList(
        prefix: String,
        values: Map<String, List<String>>,
    ): Map<String, String> = buildMap {
        require(values.keys.none { STORED_LIST_SEPARATOR in it })
        require(values.values.flatten().none { STORED_LIST_SEPARATOR in it })
        put("${prefix}keys", values.keys.joinToString(STORED_LIST_SEPARATOR))
        values.forEach { (name, entries) ->
            put("$prefix$name", entries.joinToString(STORED_LIST_SEPARATOR))
        }
    }

    private companion object {
        const val STORED_LIST_SEPARATOR = "\u001f"
        const val CONTACTS_PREFIX = "contacts."
        const val TELEPATHY_ACCOUNT_PREFIX = "/org/freedesktop/Telepathy/Account/"
    }
}

internal enum class SendTextFavoritesUpdateResult {
    Completed,
    Cancelled,
    InvalidArgument,
    PersistenceFailed,
    ActivationFailed,
}

internal enum class SendTextDispatchResult {
    Sent,
    InvalidArgument,
    UnknownMethod,
    Unavailable,
    Failed,
}

internal fun validOutgoingText(value: String): Boolean =
    value.isNotBlank() && '\u0000' !in value && value.encodeToByteArray().size <= 512

internal fun sendTextFavoriteRecords(
    favorites: List<SendTextFavorite>,
): List<Map<String, String>> = favorites.flatMap { favorite ->
    favorite.routes.map { route ->
        mapOf(
            "contactId" to checkNotNull(favorite.contactId).toString(),
            "methodId" to checkNotNull(route.methodId).toString(),
            "name" to favorite.name,
            "account" to route.accountId,
            "recipient" to route.recipient,
            "displayRecipient" to route.displayRecipient,
        )
    }
}

internal fun rockpoolSendTextContactUuid(name: String): Uuid =
    rockpoolSendTextUuid("$name.pin.rockpool.nemomobile.org")

internal fun rockpoolSendTextMethodUuid(encodedRoute: String): Uuid =
    rockpoolSendTextUuid("$encodedRoute.pin.rockpool.nemomobile.org")

private fun rockpoolSendTextUuid(name: String): Uuid {
    val namespace = java.util.UUID.fromString(DNS_NAMESPACE_UUID)
    val digest = MessageDigest.getInstance("SHA-1").run {
        update(ByteBuffer.allocate(16)
            .putLong(namespace.mostSignificantBits)
            .putLong(namespace.leastSignificantBits)
            .array())
        digest(name.encodeToByteArray())
    }
    digest[6] = ((digest[6].toInt() and 0x0f) or 0x50).toByte()
    digest[8] = ((digest[8].toInt() and 0x3f) or 0x80).toByte()
    val buffer = ByteBuffer.wrap(digest, 0, 16)
    return Uuid.parse(java.util.UUID(buffer.long, buffer.long).toString())
}

private const val DNS_NAMESPACE_UUID = "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
