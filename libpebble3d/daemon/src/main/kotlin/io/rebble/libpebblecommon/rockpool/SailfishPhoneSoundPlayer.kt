/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.plugin.PhoneSoundPlayer
import io.rebble.libpebblecommon.plugin.PluginErrors
import io.rebble.libpebblecommon.plugin.PluginNativeException
import java.io.IOException
import java.util.concurrent.CompletableFuture
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.asExecutor
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.connections.IDisconnectCallback
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.interfaces.DBus
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant

private const val FEEDBACK_INTERFACE = "com.nokia.NonGraphicFeedback1"
private const val FEEDBACK_SERVICE = "$FEEDBACK_INTERFACE.Backend"
private const val FEEDBACK_PATH = "/com/nokia/NonGraphicFeedback1"
private const val BUS_SERVICE = "org.freedesktop.DBus"
private const val BUS_PATH = "/org/freedesktop/DBus"
private const val STATUS_FAILED = 0
private const val STATUS_COMPLETED = 1
private const val PLAYBACK_TIMEOUT_MS = 60_000L

@DBusInterfaceName(FEEDBACK_INTERFACE)
internal interface SailfishNonGraphicFeedback : DBusInterface {
    // ngfd reads dictionary entries (a{sv}) despite its introspection XML declaring a(sv).
    fun Play(event: String, properties: Map<String, Variant<*>>): UInt32
    fun Stop(id: UInt32): UInt32

    class Status(path: String, val id: UInt32, val status: UInt32) : DBusSignal(path, id, status)
}

internal class SailfishPhoneSoundPlayer : PhoneSoundPlayer() {
    private val logger = Logger.withTag("SailfishPhoneSoundPlayer")
    private val lock = Any()
    private val ringing = MutableStateFlow(false)
    override val isRinging: Flow<Boolean> = ringing.asStateFlow()

    private var active: Request? = null

    override fun startRinging() {
        synchronized(lock) {
            active?.let { request ->
                if (!request.status.ended.isDone && request.connection.isConnected) return
                finish(request, request.status.ended.getNow(End.BUS_LOST))
            }
            try {
                playAlarm()
            } catch (e: Exception) {
                logger.w(e) { "Could not start the phone alarm" }
                throw PluginNativeException(PluginErrors.PLUGIN_UNAVAILABLE, "Phone alarm unavailable")
            }
        }
    }

    private fun playAlarm() {
        val status = PlaybackStatus()
        // The feedback request belongs to the unique name of this dedicated connection.
        val connection = DBusConnectionBuilder.forSystemBus()
            .withShared(false)
            .withDisconnectCallback(object : IDisconnectCallback {
                override fun disconnectOnError(error: IOException) {
                    status.ended.complete(End.BUS_LOST)
                }

                override fun clientDisconnect() {
                    status.ended.complete(End.BUS_LOST)
                }
            })
            .transportConfig().withRegisterSelf(false).back()
            .build()
        var connectionTransferred = false
        try {
            connection.register()
            val bus = connection.getRemoteObject(BUS_SERVICE, BUS_PATH, DBus::class.java)
            val owner = bus.GetNameOwner(FEEDBACK_SERVICE)
            val backend = connection.getRemoteObject(
                owner, FEEDBACK_PATH, SailfishNonGraphicFeedback::class.java, false,
            )
            connection.addSigHandler(DBus.NameOwnerChanged::class.java) { signal ->
                if (signal.source == BUS_SERVICE && signal.path == BUS_PATH &&
                    signal.name == FEEDBACK_SERVICE && signal.oldOwner == owner) {
                    status.ended.complete(End.OWNER_LOST)
                }
            }
            connection.addSigHandler(SailfishNonGraphicFeedback.Status::class.java, owner, backend) { signal ->
                if (signal.source == owner && signal.path == FEEDBACK_PATH) {
                    when (signal.status.toInt()) {
                        STATUS_FAILED -> status.finished(signal.id, End.FAILED)
                        STATUS_COMPLETED -> status.finished(signal.id, End.COMPLETED)
                    }
                }
            }
            check(bus.GetNameOwner(FEEDBACK_SERVICE) == owner) { "Feedback service owner changed" }
            val id = backend.Play(
                "rockpool-find-phone", mapOf("play.timeout" to Variant(UInt32(PLAYBACK_TIMEOUT_MS))),
            )
            check(id.toLong() != 0L) { "Feedback service rejected the alarm" }
            status.assignId(id)
            val request = Request(connection, backend, id, status)
            val end = status.ended.getNow(null)
            if (end != null) {
                if (end == End.OWNER_LOST) stop(request)
                check(end == End.COMPLETED) { "Feedback request ended before playback was accepted" }
                return
            }
            // Disconnect can wait for dbus-java's signal workers, so clean up on a separate thread.
            status.ended.thenAcceptAsync({ reason ->
                synchronized(lock) {
                    if (active === request) finish(request, reason)
                }
            }, Dispatchers.IO.asExecutor())
            active = request
            connectionTransferred = true
            ringing.value = true
        } finally {
            if (!connectionTransferred) disconnect(connection)
        }
    }

    override fun stopRinging() {
        synchronized(lock) {
            val request = active ?: return
            request.status.ended.complete(End.STOPPED)
            finish(request, request.status.ended.getNow(End.STOPPED))
        }
    }

    private fun finish(request: Request, reason: End) {
        active = null
        ringing.value = false
        try {
            when (reason) {
                End.STOPPED, End.OWNER_LOST -> stop(request)
                End.FAILED -> logger.w { "Feedback service could not play the phone alarm" }
                else -> Unit
            }
        } finally {
            disconnect(request.connection)
        }
    }

    private fun stop(request: Request) {
        try {
            if (request.connection.isConnected) request.backend.Stop(request.id)
        } catch (e: Exception) {
            logger.w(e) { "Could not stop the phone alarm" }
        }
    }

    private fun disconnect(connection: DBusConnection) {
        try {
            connection.disconnect()
        } catch (e: Exception) {
            logger.w(e) { "Could not close the phone alarm connection" }
        }
    }

    private class Request(
        val connection: DBusConnection,
        val backend: SailfishNonGraphicFeedback,
        val id: UInt32,
        val status: PlaybackStatus,
    )

    private enum class End { FAILED, COMPLETED, OWNER_LOST, BUS_LOST, STOPPED }

    private class PlaybackStatus {
        val ended = CompletableFuture<End>()
        private var requestId: UInt32? = null
        // The service can send a terminal status before replying to Play with the request ID.
        private val early = mutableMapOf<UInt32, End>()

        @Synchronized
        fun assignId(id: UInt32) {
            requestId = id
            early[id]?.let { ended.complete(it) }
            early.clear()
        }

        @Synchronized
        fun finished(id: UInt32, reason: End) {
            when (requestId) {
                null -> early.putIfAbsent(id, reason)
                id -> ended.complete(reason)
            }
        }
    }
}
