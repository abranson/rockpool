/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.freedesktop.dbus.FileDescriptor
import org.freedesktop.dbus.transport.junixsocket.JUnixSocketSocketProvider
import org.newsclub.net.unix.FileDescriptorCast
import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions

internal class InvalidInstallFileException(message: String) : IOException(message)

/** Duplicate a received descriptor before returning from its D-Bus method invocation. */
internal fun duplicateInstallFile(descriptor: FileDescriptor): FileInputStream {
    val provider = JUnixSocketSocketProvider()
    val received = descriptor.toJavaFileDescriptor(provider)
    return try {
        val duplicate = FileDescriptorCast.duplicating(received).fileDescriptor
        val input = FileInputStream(duplicate)
        try {
            val value = provider.getFileDescriptorValue(duplicate)
                .orElseThrow { InvalidInstallFileException("file descriptor is unavailable") }
            if (!Files.isRegularFile(Path.of("/proc/self/fd/$value"))) {
                throw InvalidInstallFileException("installation input is not a regular file")
            }
            input
        } catch (e: Exception) {
            input.close()
            throw e
        }
    } finally {
        // SCM_RIGHTS gives the receiver ownership of a new descriptor. dbus-java exposes its
        // integer but does not close it after dispatch, so close that original after taking the
        // operation-owned duplicate.
        runCatching { FileInputStream(received).close() }
    }
}

/**
 * Copy an untrusted descriptor into a private, bounded regular file for libpebble3.
 *
 * libpebble3's installers consume paths asynchronously, so the D-Bus descriptor cannot be handed
 * to them directly. The private copy also gives parsers a seekable input without accepting a path
 * chosen by the caller.
 */
internal suspend fun stageInstallFile(
    input: InputStream,
    suffix: String,
    maximumBytes: Long,
    directory: Path? = null,
): Path = withContext(Dispatchers.IO) {
    require(maximumBytes > 0)
    require(suffix.matches(Regex("\\.[a-z0-9]{1,8}")))
    val permissions = PosixFilePermissions.asFileAttribute(
        PosixFilePermissions.fromString("rw-------"),
    )
    val temporary = if (directory == null) {
        Files.createTempFile("rockpool-install-", suffix, permissions)
    } else {
        Files.createDirectories(directory)
        Files.createTempFile(directory, "rockpool-install-", suffix, permissions)
    }
    try {
        Files.newOutputStream(temporary).use { output ->
            val buffer = ByteArray(COPY_BUFFER_BYTES)
            var total = 0L
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = input.read(buffer)
                if (count < 0) break
                if (count == 0) continue
                total += count
                if (total > maximumBytes) {
                    throw InvalidInstallFileException("installation file is too large")
                }
                output.write(buffer, 0, count)
            }
            if (total == 0L) {
                throw InvalidInstallFileException("installation file is empty")
            }
        }
        temporary
    } catch (e: Exception) {
        Files.deleteIfExists(temporary)
        throw e
    }
}

private const val COPY_BUFFER_BYTES = 64 * 1024
