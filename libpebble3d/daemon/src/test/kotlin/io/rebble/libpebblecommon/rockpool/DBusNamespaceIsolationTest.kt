/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.bin.EmbeddedDBusDaemon
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.errors.UnknownObject
import org.freedesktop.dbus.interfaces.DBusInterface
import java.nio.file.Files
import kotlin.io.path.deleteIfExists
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotEquals

@DBusInterfaceName("org.rockpool.tests.NamespaceProbe1")
internal interface NamespaceIsolationProbe : DBusInterface {
    fun Identity(): String
}

private class NamespaceIsolationProbeObject(
    private val path: String,
    private val identity: String,
) : NamespaceIsolationProbe {
    override fun getObjectPath(): String = path

    override fun Identity(): String = identity
}

class DBusNamespaceIsolationTest {
    @Test
    fun `well known names expose only objects from their physical connection`() {
        val directory = Files.createTempDirectory("rockpool-dbus-isolation-")
        val socket = directory.resolve("bus.sock")
        val clientAddress = "unix:path=${socket.toAbsolutePath()}"
        val daemon = EmbeddedDBusDaemon("$clientAddress,listen=true")
        var primary: DBusConnection? = null
        var compatibility: DBusConnection? = null
        var client: DBusConnection? = null

        try {
            daemon.startInBackgroundAndWait(5_000)
            primary = isolatedConnection(clientAddress)
            compatibility = isolatedConnection(clientAddress)
            client = isolatedConnection(clientAddress)

            assertNotEquals(primary.uniqueName, compatibility.uniqueName)
            primary.requestBusName(PRIMARY_NAME)
            compatibility.requestBusName(COMPATIBILITY_NAME)
            primary.exportObject(PRIMARY_PATH, NamespaceIsolationProbeObject(PRIMARY_PATH, "primary"))
            compatibility.exportObject(
                COMPATIBILITY_PATH,
                NamespaceIsolationProbeObject(COMPATIBILITY_PATH, "compatibility"),
            )

            assertEquals("primary", client.probe(PRIMARY_NAME, PRIMARY_PATH).Identity())
            assertEquals(
                "compatibility",
                client.probe(COMPATIBILITY_NAME, COMPATIBILITY_PATH).Identity(),
            )

            assertFailsWith<UnknownObject> {
                client.probe(PRIMARY_NAME, COMPATIBILITY_PATH).Identity()
            }
            assertFailsWith<UnknownObject> {
                client.probe(COMPATIBILITY_NAME, PRIMARY_PATH).Identity()
            }
        } finally {
            client?.disconnect()
            compatibility?.disconnect()
            primary?.disconnect()
            daemon.close()
            socket.deleteIfExists()
            directory.deleteIfExists()
        }
    }

    private fun isolatedConnection(address: String): DBusConnection =
        DBusConnectionBuilder.forAddress(address).withShared(false).build()

    private fun DBusConnection.probe(name: String, path: String): NamespaceIsolationProbe =
        getRemoteObject(name, path, NamespaceIsolationProbe::class.java, false)

    private companion object {
        const val PRIMARY_NAME = "org.rockpool"
        const val PRIMARY_PATH = "/org/rockpool/IsolationProbe"
        const val COMPATIBILITY_NAME = "org.rockwork"
        const val COMPATIBILITY_PATH = "/org/rockwork/IsolationProbe"
    }
}
