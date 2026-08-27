/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import org.freedesktop.dbus.FileDescriptor
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.transport.junixsocket.JUnixSocketSocketProvider
import java.io.FileInputStream
import java.nio.file.Files
import java.util.concurrent.TimeUnit
import kotlin.io.path.deleteIfExists
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

@DBusInterfaceName("io.rebble.libpebble3.tests.FileDescriptorProbe1")
internal interface FileDescriptorProbe : DBusInterface {
    fun Read(descriptor: FileDescriptor): ByteArray
}

private class FileDescriptorProbeObject(
    private val path: String,
) : FileDescriptorProbe {
    @Volatile
    var receivedDescriptor = -1

    override fun getObjectPath(): String = path

    override fun Read(descriptor: FileDescriptor): ByteArray {
        receivedDescriptor = descriptor.intFileDescriptor
        return duplicateInstallFile(descriptor).use { input ->
            input.readNBytes(MAX_PROBE_BYTES)
        }
    }

    private companion object {
        const val MAX_PROBE_BYTES = 4 * 1024
    }
}

class DBusFileDescriptorTransportTest {
    @Test
    fun `unix descriptor is negotiated and transferred with its content`() {
        val directory = Files.createTempDirectory("rockpool-dbus-fd-")
        val socket = directory.resolve("bus.sock")
        val payload = directory.resolve("payload.bin")
        val expected = "rockpool-fd-transport".encodeToByteArray()
        Files.write(payload, expected)
        val daemon = startSessionBus(socket.toAbsolutePath().toString())
        val address = daemon.address
        var service: DBusConnection? = null
        var client: DBusConnection? = null

        try {
            service = isolatedConnection(address)
            client = isolatedConnection(address)
            assertTrue(service.isFileDescriptorSupported)
            assertTrue(client.isFileDescriptorSupported)

            service.requestBusName(BUS_NAME)
            val probe = FileDescriptorProbeObject(OBJECT_PATH)
            service.exportObject(OBJECT_PATH, probe)

            FileInputStream(payload.toFile()).use { input ->
                val outgoing = FileDescriptor.fromJavaFileDescriptor(
                    input.fd,
                    JUnixSocketSocketProvider(),
                )
                val remote = client.getRemoteObject(
                    BUS_NAME,
                    OBJECT_PATH,
                    FileDescriptorProbe::class.java,
                    false,
                )

                assertEquals(expected.toList(), remote.Read(outgoing).toList())
                assertNotEquals(outgoing.intFileDescriptor, probe.receivedDescriptor)
            }
        } finally {
            client?.disconnect()
            service?.disconnect()
            daemon.close()
            payload.deleteIfExists()
            socket.deleteIfExists()
            directory.deleteIfExists()
        }
    }

    private fun isolatedConnection(address: String): DBusConnection =
        DBusConnectionBuilder.forAddress(address).withShared(false).build()

    private fun startSessionBus(socket: String): RunningSessionBus {
        val process = ProcessBuilder(
            "dbus-daemon",
            "--session",
            "--nofork",
            "--nopidfile",
            "--nosyslog",
            "--address=unix:path=$socket",
            "--print-address=1",
        ).start()
        val address = process.inputStream.bufferedReader().readLine()
        check(!address.isNullOrBlank()) {
            process.destroyForcibly()
            "dbus-daemon did not publish an address"
        }
        return RunningSessionBus(address, process)
    }

    private class RunningSessionBus(
        val address: String,
        private val process: Process,
    ) : AutoCloseable {
        override fun close() {
            process.destroy()
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                process.destroyForcibly()
                process.waitFor(5, TimeUnit.SECONDS)
            }
        }
    }

    private companion object {
        const val BUS_NAME = "io.rebble.libpebble3.tests.FileDescriptorTransport"
        const val OBJECT_PATH = "/io/rebble/libpebble3/tests/FileDescriptorTransport"
    }
}
