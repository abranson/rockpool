/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import org.freedesktop.dbus.connections.transports.AbstractTransport
import org.freedesktop.dbus.connections.transports.TransportBuilder
import org.freedesktop.dbus.messages.MethodCall
import org.freedesktop.dbus.messages.MethodReturn
import org.freedesktop.dbus.types.UInt32
import java.nio.file.Files
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.io.path.deleteIfExists
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SailfishPairingRequesterTest {
    @Test
    fun `pairing continues when the Sailfish broker sends no reply`() {
        val directory = Files.createTempDirectory("rockpool-pairing-")
        val socket = directory.resolve("bus.sock")
        val output = directory.resolve("requester.log")
        val bus = ProcessBuilder(
            "dbus-daemon", "--session", "--nofork", "--nopidfile", "--nosyslog",
            "--address=unix:path=$socket", "--print-address=1",
        ).start()
        val executor = Executors.newSingleThreadExecutor()
        var broker: AbstractTransport? = null
        var requester: Process? = null
        try {
            val address = bus.inputStream.bufferedReader().readLine()
            broker = TransportBuilder.create(address).build()
            broker.connect()
            broker.busCall("Hello")
            broker.busCall("RequestName", "su", "com.jolla.lipstick", UInt32(0))
            val transport = broker
            val received = executor.submit<MethodCall> {
                while (true) {
                    val message = transport.readMessage()
                    if (message is MethodCall) return@submit message
                }
                @Suppress("UNREACHABLE_CODE")
                error("unreachable")
            }
            requester = ProcessBuilder(
                "${System.getProperty("java.home")}/bin/java", "-cp", System.getProperty("java.class.path"),
                SailfishPairingRequesterProbe::class.java.name,
            ).apply {
                environment()["DBUS_SESSION_BUS_ADDRESS"] = address
                redirectErrorStream(true)
                redirectOutput(output.toFile())
            }.start()

            // Lipstick's QML adaptor invokes its pairWithDevice signal and does not reply.
            // Keep the broker alive without replying: returning from the request must not
            // depend on pairing completion, a bus timeout, or the broker disconnecting.
            val call = received.get(5, TimeUnit.SECONDS)
            assertEquals("/bluetooth", call.path)
            assertEquals("com.jolla.lipstick", call.getInterface())
            assertEquals("pairWithDevice", call.name)
            assertEquals(listOf("AA:BB:CC:DD:EE:FF"), call.parameters.toList())
            assertTrue(requester.waitFor(5, TimeUnit.SECONDS), "requester waited for a broker reply")
            assertEquals(0, requester.exitValue(), Files.readString(output))
            assertTrue(call.flags.toInt() and 1 != 0, "pairing request must not expect a reply")
        } finally {
            requester?.destroyForcibly()
            requester?.waitFor(5, TimeUnit.SECONDS)
            broker?.close()
            executor.shutdownNow()
            bus.destroy()
            if (!bus.waitFor(5, TimeUnit.SECONDS)) {
                bus.destroyForcibly()
                bus.waitFor(5, TimeUnit.SECONDS)
            }
            output.deleteIfExists()
            socket.deleteIfExists()
            directory.deleteIfExists()
        }
    }

    private fun AbstractTransport.busCall(member: String, signature: String? = null, vararg args: Any) {
        val call = messageFactory.createMethodCall(
            "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            member, 0, signature, *args,
        )
        writeMessage(call)
        while (true) {
            val reply = readMessage()
            if (reply is MethodReturn && reply.replySerial == call.serial) return
        }
    }
}

internal object SailfishPairingRequesterProbe {
    @JvmStatic
    fun main(args: Array<String>) {
        check(SailfishPairingRequester().requestPairing("AA:BB:CC:DD:EE:FF"))
    }
}
