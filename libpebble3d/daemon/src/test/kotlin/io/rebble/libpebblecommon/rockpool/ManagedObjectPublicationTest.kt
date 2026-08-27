/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import java.io.IOException
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue

class ManagedObjectPublicationTest {
    private class FakeConnection

    @Test
    fun `snapshot reader cannot observe logical paths before reconnect export completes`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val fakeConnection = FakeConnection()
        var connection: FakeConnection? = null
        val logicalPaths = listOf("/io/rebble/libpebble3/watch/one", "/io/rebble/libpebble3/operation/one")
        val exportedPaths = mutableListOf<String>()
        val exportEntered = CountDownLatch(1)
        val releaseExport = CountDownLatch(1)
        val exportCompleted = CountDownLatch(1)
        val snapshotStarted = CountDownLatch(1)
        val snapshotFinished = CountDownLatch(1)
        val workerFailure = AtomicReference<Throwable?>()
        val snapshot = AtomicReference<List<String>?>()
        val snapshotSawCompletedExport = AtomicReference<Boolean?>()
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = { connection = it },
        )

        val reconnectThread = thread(name = "managed-object-reconnect") {
            try {
                publisher.publishConnectionAfterExport(fakeConnection) {
                    exportedPaths += logicalPaths
                    exportEntered.countDown()
                    check(releaseExport.await(5, TimeUnit.SECONDS))
                    exportCompleted.countDown()
                }
            } catch (t: Throwable) {
                workerFailure.set(t)
            }
        }

        assertTrue(exportEntered.await(1, TimeUnit.SECONDS))
        val snapshotThread = thread(name = "managed-object-reconnect-snapshot") {
            snapshotStarted.countDown()
            synchronized(objectsLock) {
                snapshot.set(logicalPaths.toList())
                snapshotSawCompletedExport.set(exportCompleted.count == 0L)
            }
            snapshotFinished.countDown()
        }
        try {
            assertTrue(snapshotStarted.await(1, TimeUnit.SECONDS))
            assertFalse(snapshotFinished.await(100, TimeUnit.MILLISECONDS))
            assertNull(connection)
        } finally {
            releaseExport.countDown()
        }

        reconnectThread.join(2_000)
        snapshotThread.join(2_000)
        assertFalse(reconnectThread.isAlive)
        assertFalse(snapshotThread.isAlive)
        workerFailure.get()?.let { throw it }
        assertEquals(logicalPaths, exportedPaths)
        assertEquals(logicalPaths, snapshot.get())
        assertEquals(true, snapshotSawCompletedExport.get())
        assertSame(fakeConnection, connection)
    }

    @Test
    fun `complete snapshot is exported before the public name and internal connection`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val fakeConnection = FakeConnection()
        var connection: FakeConnection? = null
        val events = mutableListOf<String>()
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = {
                events += "internal connection"
                connection = it
            },
        )

        publisher.publishConnectionAfterExport(
            connection = fakeConnection,
            publishPublicName = {
                assertNull(connection)
                events += "public name"
            },
        ) {
            events += "root object"
            events += "static objects"
            events += "dynamic objects"
        }

        assertEquals(
            listOf(
                "root object",
                "static objects",
                "dynamic objects",
                "public name",
                "internal connection",
            ),
            events,
        )
        assertSame(fakeConnection, connection)
    }

    @Test
    fun `failed public name publication leaves the connection unpublished`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val fakeConnection = FakeConnection()
        var connection: FakeConnection? = null
        val events = mutableListOf<String>()
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = {
                events += "internal connection"
                connection = it
            },
        )

        val failure = kotlin.test.assertFailsWith<IOException> {
            publisher.publishConnectionAfterExport(
                connection = fakeConnection,
                publishPublicName = {
                    events += "public name"
                    throw IOException("name publication failed")
                },
            ) {
                events += "snapshot"
            }
        }

        assertEquals("name publication failed", failure.message)
        assertEquals(listOf("snapshot", "public name"), events)
        assertNull(connection)
    }

    @Test
    fun `failed reconnect export leaves the connection unpublished`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val oldConnection = FakeConnection()
        val reconnectConnection = FakeConnection()
        var connection: FakeConnection? = oldConnection
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = { connection = it },
        )

        val failure = kotlin.test.assertFailsWith<IOException> {
            publisher.publishConnectionAfterExport(reconnectConnection) {
                throw IOException("snapshot export failed")
            }
        }

        assertEquals("snapshot export failed", failure.message)
        assertSame(oldConnection, connection)
    }

    @Test
    fun `publish exports before a snapshot can observe the object`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val fakeConnection = FakeConnection()
        var connection: FakeConnection? = fakeConnection
        val visible = linkedSetOf<String>()
        val events = Collections.synchronizedList(mutableListOf<String>())
        val exportEntered = CountDownLatch(1)
        val releaseExport = CountDownLatch(1)
        val snapshotStarted = CountDownLatch(1)
        val snapshotFinished = CountDownLatch(1)
        val workerFailure = AtomicReference<Throwable?>()
        val mutationResult = AtomicReference<ManagedObjectMutationResult<FakeConnection, Unit>?>()
        var snapshot = false
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = { connection = it },
        )

        val mutationThread = thread(name = "managed-object-publish") {
            try {
                mutationResult.set(publisher.mutate {
                    publish(
                        export = {
                            events += "export"
                            exportEntered.countDown()
                            check(releaseExport.await(5, TimeUnit.SECONDS))
                        },
                        makeVisible = {
                            events += "visible"
                            visible += "watch"
                        },
                    )
                })
            } catch (t: Throwable) {
                workerFailure.set(t)
            }
        }

        assertTrue(exportEntered.await(1, TimeUnit.SECONDS))
        val snapshotThread = thread(name = "managed-object-snapshot") {
            snapshotStarted.countDown()
            snapshot = synchronized(objectsLock) { "watch" in visible }
            snapshotFinished.countDown()
        }
        try {
            assertTrue(snapshotStarted.await(1, TimeUnit.SECONDS))
            assertFalse(snapshotFinished.await(100, TimeUnit.MILLISECONDS))
        } finally {
            releaseExport.countDown()
        }

        mutationThread.join(2_000)
        snapshotThread.join(2_000)
        assertFalse(mutationThread.isAlive)
        assertFalse(snapshotThread.isAlive)
        workerFailure.get()?.let { throw it }
        assertTrue(snapshot)
        assertEquals(listOf("export", "visible"), events)
        assertSame(fakeConnection, mutationResult.get()?.connection)
        assertNull(mutationResult.get()?.failure)
    }

    @Test
    fun `remove unexports before a snapshot can observe the object as hidden`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val fakeConnection = FakeConnection()
        var connection: FakeConnection? = fakeConnection
        val visible = linkedSetOf("operation")
        val events = Collections.synchronizedList(mutableListOf<String>())
        val unexportEntered = CountDownLatch(1)
        val releaseUnexport = CountDownLatch(1)
        val snapshotStarted = CountDownLatch(1)
        val snapshotFinished = CountDownLatch(1)
        val workerFailure = AtomicReference<Throwable?>()
        val mutationResult = AtomicReference<ManagedObjectMutationResult<FakeConnection, Unit>?>()
        var snapshot = true
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = { connection = it },
        )

        val mutationThread = thread(name = "managed-object-remove") {
            try {
                mutationResult.set(publisher.mutate {
                    remove(
                        unexport = {
                            events += "unexport"
                            unexportEntered.countDown()
                            check(releaseUnexport.await(5, TimeUnit.SECONDS))
                        },
                        hide = {
                            events += "hidden"
                            visible -= "operation"
                        },
                    )
                })
            } catch (t: Throwable) {
                workerFailure.set(t)
            }
        }

        assertTrue(unexportEntered.await(1, TimeUnit.SECONDS))
        val snapshotThread = thread(name = "managed-object-snapshot") {
            snapshotStarted.countDown()
            snapshot = synchronized(objectsLock) { "operation" in visible }
            snapshotFinished.countDown()
        }
        try {
            assertTrue(snapshotStarted.await(1, TimeUnit.SECONDS))
            assertFalse(snapshotFinished.await(100, TimeUnit.MILLISECONDS))
        } finally {
            releaseUnexport.countDown()
        }

        mutationThread.join(2_000)
        snapshotThread.join(2_000)
        assertFalse(mutationThread.isAlive)
        assertFalse(snapshotThread.isAlive)
        workerFailure.get()?.let { throw it }
        assertFalse(snapshot)
        assertEquals(listOf("unexport", "hidden"), events)
        assertSame(fakeConnection, mutationResult.get()?.connection)
        assertNull(mutationResult.get()?.failure)
    }

    @Test
    fun `connection failure detaches but preserves the logical mutation`() {
        val objectsLock = Any()
        val connectionLock = Any()
        val fakeConnection = FakeConnection()
        var connection: FakeConnection? = fakeConnection
        var visible = false
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = { connection = it },
        )

        val result = publisher.mutate {
            publish(
                export = { throw IOException("connection lost") },
                makeVisible = { visible = true },
            )
        }

        assertTrue(visible)
        assertNull(connection)
        assertNull(result.connection)
        assertSame(fakeConnection, result.lostConnection)
        assertIs<IOException>(result.failure)
    }

    @Test
    fun `disconnected mutation is retained for the reconnect snapshot`() {
        val objectsLock = Any()
        val connectionLock = Any()
        var connection: FakeConnection? = null
        var exportCalls = 0
        var visible = false
        val publisher = ManagedObjectPublisher(
            objectsLock = objectsLock,
            connectionLock = connectionLock,
            currentConnection = { connection },
            setConnection = { connection = it },
        )

        val result = publisher.mutate {
            publish(
                export = { exportCalls += 1 },
                makeVisible = { visible = true },
            )
        }

        assertTrue(visible)
        assertEquals(0, exportCalls)
        assertNull(result.connection)
        assertNull(result.lostConnection)
        assertNull(result.failure)
    }
}
