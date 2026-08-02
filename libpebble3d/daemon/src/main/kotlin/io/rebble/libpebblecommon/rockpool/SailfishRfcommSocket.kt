/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.bt.classic.transport.LinuxRfcommSocket
import io.rebble.libpebblecommon.connection.bt.classic.transport.LinuxRfcommSocketFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

internal interface RfcommNativeApi {
    val available: Boolean
    fun create(address: String, channel: Int): Long
    fun connect(handle: Long, timeoutMillis: Int): Int
    fun read(handle: Long, buffer: ByteArray, offset: Int, length: Int): Int
    fun write(handle: Long, buffer: ByteArray, offset: Int, length: Int): Int
    fun shutdown(handle: Long)
    fun destroy(handle: Long)
}

internal object JniRfcommNativeApi : RfcommNativeApi {
    override val available: Boolean
        get() = PlatformProviderController.nativeLibraryAvailable()

    override fun create(address: String, channel: Int): Long =
        PlatformProviderNative.rfcommCreate(address, channel)

    override fun connect(handle: Long, timeoutMillis: Int): Int =
        PlatformProviderNative.rfcommConnect(handle, timeoutMillis)

    override fun read(handle: Long, buffer: ByteArray, offset: Int, length: Int): Int =
        PlatformProviderNative.rfcommRead(handle, buffer, offset, length)

    override fun write(handle: Long, buffer: ByteArray, offset: Int, length: Int): Int =
        PlatformProviderNative.rfcommWrite(handle, buffer, offset, length)

    override fun shutdown(handle: Long) = PlatformProviderNative.rfcommShutdown(handle)

    override fun destroy(handle: Long) = PlatformProviderNative.rfcommDestroy(handle)
}

internal class SailfishRfcommSocketFactory(
    private val native: RfcommNativeApi = JniRfcommNativeApi,
) : LinuxRfcommSocketFactory {
    internal val available: Boolean
        get() = native.available

    override fun create(address: String, channel: Int): LinuxRfcommSocket? {
        if (!available) return null
        val handle = runCatching { native.create(address, channel) }.getOrDefault(0)
        return handle.takeIf { it != 0L }?.let { SailfishRfcommSocket(native, it) }
    }
}

private class SailfishRfcommSocket(
    private val native: RfcommNativeApi,
    private val handle: Long,
) : LinuxRfcommSocket {
    private val lifecycleLock = Any()
    private val closed = AtomicBoolean(false)

    override suspend fun connect(timeoutMillis: Long): Boolean {
        require(timeoutMillis in 1..MAX_CONNECT_TIMEOUT_MILLIS)
        val result = withContext(Dispatchers.IO) {
            native.connect(handle, timeoutMillis.toInt())
        }
        return when {
            result == 0 -> true
            result < 0 -> false
            else -> throw IOException("invalid RFCOMM connect result: $result")
        }
    }

    override suspend fun read(buffer: ByteArray): Int {
        require(buffer.isNotEmpty())
        val result = withContext(Dispatchers.IO) {
            native.read(handle, buffer, 0, buffer.size)
        }
        return when {
            result > buffer.size -> throw IOException("invalid RFCOMM read result: $result")
            result > 0 -> result
            result == 0 -> -1
            else -> throw IOException("RFCOMM read failed: ${-result}")
        }
    }

    override suspend fun write(buffer: ByteArray, offset: Int, length: Int): Int {
        require(offset >= 0 && offset <= buffer.size)
        require(length > 0 && length <= buffer.size - offset)
        val result = withContext(Dispatchers.IO) {
            native.write(handle, buffer, offset, length)
        }
        return when {
            result in 1..length -> result
            result == 0 -> throw IOException("RFCOMM write made no progress")
            result < 0 -> throw IOException("RFCOMM write failed: ${-result}")
            else -> throw IOException("invalid RFCOMM write result: $result")
        }
    }

    override suspend fun flush() = Unit

    override fun shutdown() {
        synchronized(lifecycleLock) {
            if (!closed.get()) native.shutdown(handle)
        }
    }

    override suspend fun close() {
        withContext(NonCancellable + Dispatchers.IO) {
            synchronized(lifecycleLock) {
                if (closed.compareAndSet(false, true)) native.destroy(handle)
            }
        }
    }

    private companion object {
        const val MAX_CONNECT_TIMEOUT_MILLIS = 120_000L
    }
}
