/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.ConnectedPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.packets.AppMessage
import io.rebble.libpebblecommon.packets.ProtocolCapsFlag
import io.rebble.libpebblecommon.permissions.AppPermissions
import io.rebble.libpebblecommon.permissions.PermissionDecision
import io.rebble.libpebblecommon.plugin.PluginPermissions
import io.rebble.libpebblecommon.services.appmessage.AppMessageData
import io.rebble.libpebblecommon.services.appmessage.AppMessageResult
import java.io.ByteArrayOutputStream
import java.io.EOFException
import java.io.InputStream
import java.io.IOException
import java.net.InetSocketAddress
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.Base64
import java.util.concurrent.ConcurrentHashMap
import javax.net.ssl.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.*
import kotlin.uuid.Uuid

internal class Droidpool(private val settings: RockpoolSettings) {
    private val addressKey = "droidpool.watch"
    private val codeKey = "droidpool.pairing"
    private val pairing = MutableStateFlow(settings.get(addressKey) to settings.get(codeKey))
    val pairedWatch: String get() = pairing.value.first

    @Synchronized
    fun configure(address: String, code: String): Boolean {
        if (code.isNotEmpty() && (address.isBlank() || !Regex("[A-Za-z0-9_-]{43}").matches(code) ||
                Base64.getUrlEncoder().withoutPadding().encodeToString(Base64.getUrlDecoder().decode(code)) != code)) return false
        val watch = if (code.isEmpty()) "" else address
        if (!settings.setAllChecked(mapOf(addressKey to watch, codeKey to code))) return false
        pairing.value = watch to code
        return true
    }

    fun start(scope: CoroutineScope, lib: LibPebble, permissions: AppPermissions) = scope.launch {
        pairing.collectLatest { (address, code) ->
            if (code.isEmpty()) return@collectLatest
            val key = Base64.getUrlDecoder().decode(code)
            combine(lib.watches, lib.config) { watches, config ->
                watches.filterIsInstance<ConnectedPebbleDevice>().firstOrNull {
                    config.watchConfig.enableLegacyPebbleKit && it.identifier.asString.equals(address, true)
                }
            }.distinctUntilChangedBy { it?.runningApp }.collectLatest watchChanged@{ watch ->
                if (watch == null) return@watchChanged
                combine(watch.runningApp, watch.currentCompanionAppSessions) { id, sessions ->
                    id.takeIf { sessions.isEmpty() }
                }.distinctUntilChanged().collectLatest appChanged@{ uuid ->
                    if (uuid == null) return@appChanged
                    val entry = lib.getLockerApp(uuid).first() as? LockerWrapper.NormalApp ?: return@appChanged
                    val subject = permissions.subjectOf(uuid) ?: return@appChanged
                    val permission = PluginPermissions.LEGACY_PEBBLEKIT
                    // An undeclared legacy companion asks for access on its first watch message.
                    val trigger = launch {
                        if (entry.properties.androidCompanion != null) permissions.request(subject, permission)
                        else watch.inboundMessages.filterIsInstance<AppMessage.AppMessagePush>()
                            .first { it.uuid.get() == uuid }.also { permissions.request(subject, permission) }
                    }
                    try {
                        permissions.grantedKeys(subject, listOf(permission)).map { permission in it }
                            .distinctUntilChanged().collectLatest { allowed ->
                                if (allowed) connect(watch, uuid, key) {
                                    watch.runningApp.value == uuid && watch.currentCompanionAppSessions.value.isEmpty() &&
                                        pairing.value == (address to code) &&
                                        permissions.request(subject, permission) == PermissionDecision.Granted
                                }
                            }
                    } finally {
                        trigger.cancel()
                    }
                }
            }
        }
    }

    private suspend fun connect(watch: ConnectedPebbleDevice, uuid: Uuid, key: ByteArray, allowed: suspend () -> Boolean) {
        val trust = object : X509TrustManager {
            override fun getAcceptedIssuers() = emptyArray<X509Certificate>()
            override fun checkClientTrusted(chain: Array<X509Certificate>, auth: String) = throw CertificateException()
            override fun checkServerTrusted(chain: Array<X509Certificate>, auth: String) {
                val cert = chain.firstOrNull() ?: throw CertificateException()
                val digest = MessageDigest.getInstance("SHA-256").digest(cert.encoded)
                if (!MessageDigest.isEqual(digest.copyOf(16), key.copyOf(16))) throw CertificateException()
            }
        }
        val context = SSLContext.getInstance("TLS").apply { init(null, arrayOf(trust), null) }
        val token = Base64.getUrlEncoder().withoutPadding().encodeToString(key.copyOfRange(16, 32))
        var backoff = 500L
        while (currentCoroutineContext().isActive) {
            val socket = context.socketFactory.createSocket() as SSLSocket
            try {
                coroutineScope {
                    val session = launch(Dispatchers.IO) {
                        socket.connect(InetSocketAddress("127.0.0.1", 51841), 3000)
                        socket.soTimeout = 5000
                        socket.startHandshake()
                        if (!allowed()) return@launch
                        val output = socket.outputStream
                        fun write(frame: JsonObject) = synchronized(output) {
                            output.write((frame.toString() + "\n").toByteArray())
                            output.flush()
                        }
                        write(buildJsonObject { put("op", "open"); put("uuid", uuid.toString()); put("token", token) })
                        val input = socket.inputStream.buffered()
                        val ready = readFrame(input)
                        require(ready["op"]?.jsonPrimitive?.content == "ready" && ready["uuid"]?.jsonPrimitive?.content == uuid.toString())
                        socket.soTimeout = 0
                        backoff = 500L
                        exchange(socket, input, watch, uuid, allowed, ::write)
                    }
                    try { session.join() } finally { socket.close() }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (_: IOException) {
                // AppSupport may be stopped or restarting.
            } catch (_: IllegalArgumentException) {
                // Drop an invalid session.
            } finally { socket.close() }
            delay(backoff)
            backoff = (backoff * 2).coerceAtMost(15000)
        }
    }

    private suspend fun exchange(socket: SSLSocket, input: InputStream, watch: ConnectedPebbleDevice,
                                 uuid: Uuid, allowed: suspend () -> Boolean, write: (JsonObject) -> Unit) = coroutineScope {
        val pending = ConcurrentHashMap.newKeySet<Int>()
        fun emit(op: String, tx: Int, data: JsonArray? = null) = write(buildJsonObject {
            put("op", op)
            put("uuid", uuid.toString())
            put("tx", tx)
            if (data != null) put("data", data)
        })
        val sender = launch(Dispatchers.IO) {
            try {
                watch.inboundMessages.filterIsInstance<AppMessage.AppMessagePush>().collect { message ->
                    if (message.uuid.get() == uuid && allowed()) {
                        val tx = message.transactionId.get().toInt()
                        pending.add(tx)
                        emit("data", tx, encodeDroidpoolDictionary(message.dictionary.list))
                    }
                }
            } finally { socket.close() }
        }
        try {
            val limit = if (ProtocolCapsFlag.Supports8kAppMessage in watch.capabilities) 8222 else 2048
            while (isActive) {
                val frame = readFrame(input)
                require(frame["uuid"]?.jsonPrimitive?.content == uuid.toString())
                val tx = frame.getValue("tx").jsonPrimitive.int
                when (val op = frame.getValue("op").jsonPrimitive.content) {
                    "data" -> {
                        val data = runCatching { decodeDroidpoolDictionary(frame.getValue("data").jsonArray, limit) }.getOrNull()
                        val ack = data != null && allowed() && watch.sendAppMessage(
                            AppMessageData(watch.transactionSequence.next(), uuid, data)) is AppMessageResult.ACK
                        emit(if (ack) "ack" else "nack", tx)
                    }
                    "ack", "nack" -> if (pending.remove(tx) && allowed()) {
                        watch.sendAppMessageResult(if (op == "ack") AppMessageResult.ACK(tx.toUByte()) else AppMessageResult.NACK(tx.toUByte()))
                    }
                    else -> throw IllegalArgumentException("Invalid operation")
                }
            }
        } finally { sender.cancel() }
    }

    private fun readFrame(input: InputStream): JsonObject {
        val bytes = ByteArrayOutputStream()
        while (bytes.size() < 65536) {
            when (val byte = input.read()) {
                -1 -> throw EOFException()
                10 -> return Json.parseToJsonElement(bytes.toString("UTF-8")).jsonObject
                else -> bytes.write(byte)
            }
        }
        throw IOException("Frame too large")
    }
}
