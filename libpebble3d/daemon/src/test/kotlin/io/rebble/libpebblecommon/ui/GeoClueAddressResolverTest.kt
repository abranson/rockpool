/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
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
import kotlin.test.assertNull

class GeoClueAddressResolverTest {
    private val coordinates = RockpoolWeatherCoordinates(48.85341, 2.3488, 17.5)

    @Test
    fun `locality is the current location name and receives the acquired coordinates`() = runBlocking {
        var received: RockpoolWeatherCoordinates? = null
        val resolver = GeoClueCurrentLocationNameResolver { value ->
            received = value
            mapOf("country" to "France", "locality" to "  Paris  ")
        }

        assertEquals("Paris", resolver.resolve(coordinates))
        assertEquals(coordinates, received)
    }

    @Test
    fun `missing invalid unavailable and timed out locality preserve the fallback`() = runBlocking {
        assertNull(GeoClueCurrentLocationNameResolver { emptyMap() }.resolve(coordinates))
        assertNull(
            GeoClueCurrentLocationNameResolver { mapOf("locality" to " ") }
                .resolve(coordinates),
        )
        assertNull(
            GeoClueCurrentLocationNameResolver { error("provider missing") }
                .resolve(coordinates),
        )
        assertNull(
            GeoClueCurrentLocationNameResolver(
                source = { delay(100); mapOf("locality" to "Too late") },
                timeoutMillis = 10,
            ).resolve(coordinates),
        )
    }

    @Test
    fun `dbus source forwards coordinates and accuracy to the offline provider`() = runBlocking {
        val directory = Files.createTempDirectory("rockpool-geoclue-reverse-")
        val socket = directory.resolve("bus.sock")
        val address = "unix:path=${socket.toAbsolutePath()}"
        val bus = ProcessBuilder(
            "dbus-daemon", "--session", "--nofork", "--nopidfile", "--nosyslog",
            "--address=$address", "--print-address=1",
        ).start()
        val executor = Executors.newSingleThreadExecutor()
        var provider: AbstractTransport? = null
        try {
            bus.inputStream.bufferedReader().readLine()
            provider = TransportBuilder.create(address).build()
            provider.connect()
            provider.busCall("Hello")
            provider.busCall("RequestName", "su", PROVIDER_SERVICE, UInt32(0))
            val source = DbusGeoClueReverseSource(
                connect = {
                    DBusConnectionBuilder.forAddress(address).withShared(false).build()
                },
            )
            val lookup = executor.submit<Map<String, String>?> {
                runBlocking { source.addressFor(coordinates) }
            }

            val addReference = provider.readMethodCall("AddReference")
            assertEquals(PROVIDER_PATH, addReference.path)
            assertEquals("org.freedesktop.Geoclue", addReference.getInterface())
            provider.writeMessage(provider.messageFactory.createMethodReturn(addReference, null))

            val reverse = provider.readMethodCall("PositionToAddress")
            assertEquals(PROVIDER_PATH, reverse.path)
            assertEquals("org.freedesktop.Geoclue.ReverseGeocode", reverse.getInterface())
            assertEquals(48.85341, reverse.parameters[0])
            assertEquals(2.3488, reverse.parameters[1])
            val accuracy = reverse.parameters[2] as Array<*>
            assertEquals(listOf(6, 17.5, 0.0), accuracy.toList())
            provider.writeMessage(
                provider.messageFactory.createMethodReturn(
                    reverse,
                    "a{ss}(idd)",
                    mapOf("locality" to "Paris"),
                    GeoClueAccuracy(level = 3, horizontal = 17.5, vertical = 0.0),
                ),
            )

            val removeReference = provider.readMethodCall("RemoveReference")
            assertEquals(PROVIDER_PATH, removeReference.path)
            assertEquals("org.freedesktop.Geoclue", removeReference.getInterface())
            provider.writeMessage(provider.messageFactory.createMethodReturn(removeReference, null))

            assertEquals(mapOf("locality" to "Paris"), lookup.get(5, TimeUnit.SECONDS))
        } finally {
            provider?.close()
            executor.shutdownNow()
            bus.destroy()
            if (!bus.waitFor(5, TimeUnit.SECONDS)) {
                bus.destroyForcibly()
                bus.waitFor(5, TimeUnit.SECONDS)
            }
            socket.deleteIfExists()
            directory.deleteIfExists()
        }
    }

    private fun AbstractTransport.readMethodCall(member: String): MethodCall {
        while (true) {
            val message = readMessage()
            if (message is MethodCall && message.name == member) return message
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

private const val PROVIDER_SERVICE = "org.freedesktop.Geoclue.Providers.GeonamesOffline"
private const val PROVIDER_PATH = "/org/freedesktop/Geoclue/Providers/GeonamesOffline"
