/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SailfishRfcommSocketTest {
    @Test
    fun unavailableOrFailedNativeCreateReturnsNoSocket() {
        assertNull(SailfishRfcommSocketFactory(FakeNative(available = false)).create(ADDRESS, 1))
        assertNull(SailfishRfcommSocketFactory(FakeNative(createResult = 0)).create(ADDRESS, 1))
    }

    @Test
    fun translatesNativeIoAndDestroysOnlyOnce() = runBlocking {
        val native = FakeNative()
        val socket = assertNotNull(SailfishRfcommSocketFactory(native).create(ADDRESS, 1))

        assertEquals(listOf("create:$ADDRESS:1"), native.events)
        assertTrue(socket.connect(30_000))
        native.readBytes = byteArrayOf(1, 2, 3)
        val input = ByteArray(8)
        assertEquals(3, socket.read(input))
        assertContentEquals(byteArrayOf(1, 2, 3), input.copyOf(3))
        assertEquals(2, socket.write(byteArrayOf(4, 5, 6), 1, 2))
        assertContentEquals(byteArrayOf(5, 6), native.written)

        socket.shutdown()
        socket.close()
        socket.close()
        assertEquals(1, native.events.count { it == "shutdown" })
        assertEquals(1, native.events.count { it == "destroy" })
        assertTrue(native.events.indexOf("shutdown") < native.events.indexOf("destroy"))
    }

    @Test
    fun nativeConnectFailureAndEofAreTranslated() = runBlocking {
        val native = FakeNative(connectResult = -110, readResult = 0)
        val socket = assertNotNull(SailfishRfcommSocketFactory(native).create(ADDRESS, 1))

        assertFalse(socket.connect(30_000))
        assertEquals(-1, socket.read(ByteArray(1)))
        socket.close()
    }

    private class FakeNative(
        override val available: Boolean = true,
        private val createResult: Long = 42,
        private val connectResult: Int = 0,
        private val readResult: Int? = null,
    ) : RfcommNativeApi {
        val events = mutableListOf<String>()
        var readBytes = byteArrayOf()
        var written = byteArrayOf()

        override fun create(address: String, channel: Int): Long {
            events += "create:$address:$channel"
            return createResult
        }

        override fun connect(handle: Long, timeoutMillis: Int): Int {
            events += "connect:$handle:$timeoutMillis"
            return connectResult
        }

        override fun read(
            handle: Long,
            buffer: ByteArray,
            offset: Int,
            length: Int,
        ): Int {
            readResult?.let { return it }
            readBytes.copyInto(buffer, offset)
            return readBytes.size
        }

        override fun write(
            handle: Long,
            buffer: ByteArray,
            offset: Int,
            length: Int,
        ): Int {
            written = buffer.copyOfRange(offset, offset + length)
            return length
        }

        override fun shutdown(handle: Long) {
            events += "shutdown"
        }

        override fun destroy(handle: Long) {
            events += "destroy"
        }
    }

    private companion object {
        const val ADDRESS = "02:11:22:33:12:34"
    }
}
