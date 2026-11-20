package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.plugin.PluginErrors
import io.rebble.libpebblecommon.plugin.PluginNativeException
import io.rebble.libpebblecommon.plugin.files.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.file.Path
import java.security.MessageDigest

/** Folder handles belong to the session user. All file operations run in the unprivileged daemon. */
internal class SailfishPluginFileSystem(private val settings: RockpoolSettings) : PluginFileSystem {
    private data class Folder(val path: String, val device: Long, val inode: Long) {
        val record get() = "$device:$inode:$path"
        val scope get() = MessageDigest.getInstance("SHA-256").digest(record.encodeToByteArray()).toHexString()
    }

    private fun stored(scope: String): Folder? {
        if (!scopeId.matches(scope)) return null
        val fields = settings.get("plugin.folder.$scope").split(':', limit = 3)
        if (fields.size != 3) return null
        val folder = Folder(fields[2], fields[0].toLongOrNull() ?: return null, fields[1].toLongOrNull() ?: return null)
        return folder.takeIf { it.scope == scope }
    }

    private fun probe(path: String): Folder? {
        if (!path.startsWith('/') || '\u0000' in path || Path.of(path).normalize().toString() != path) return null
        if (!PlatformProviderController.nativeLibraryAvailable()) return null
        val reply = PlatformFilesNative.request(PROBE, path.encodeToByteArray(), 0, 0, emptyArray(), null, false)
        if (reply.size != 17 || reply[0].toInt() != 0) return null
        val buffer = ByteBuffer.wrap(reply, 1, 16)
        return Folder(path, buffer.long, buffer.long)
    }

    suspend fun scopeForFolder(path: String): String? = withContext(Dispatchers.IO) { probe(path)?.scope }

    fun pathForScope(scope: String): String? = stored(scope)?.path

    override suspend fun retain(scope: String, handle: String) = withContext(Dispatchers.IO) {
        val folder = probe(handle)?.takeIf { it.scope == scope } ?: denied()
        check(settings.setChecked("plugin.folder.$scope", folder.record)) { "Could not save folder access" }
    }

    override suspend fun release(scope: String) {
        if (scopeId.matches(scope)) withContext(Dispatchers.IO) {
            check(settings.replacePrefix("plugin.folder.$scope", emptyMap())) { "Could not release folder access" }
        }
    }

    override suspend fun hasAccess(scope: String): Boolean = withContext(Dispatchers.IO) {
        val folder = stored(scope) ?: return@withContext false
        probe(folder.path) == folder
    }

    private suspend fun request(operation: Int, scope: String, parts: List<String>,
                                payload: ByteArray? = null, flag: Boolean = false): ByteBuffer? = withContext(Dispatchers.IO) {
        val folder = stored(scope) ?: denied()
        if (!PlatformProviderController.nativeLibraryAvailable()) denied()
        val names = PluginFilePath.validate(parts).map { it.encodeToByteArray(throwOnInvalidSequence = true) }.toTypedArray()
        val reply = PlatformFilesNative.request(operation, folder.path.encodeToByteArray(), folder.device, folder.inode,
            names, payload, flag)
        when (reply.firstOrNull()?.toInt()) {
            0 -> ByteBuffer.wrap(reply, 1, reply.size - 1)
            1 -> if (operation == STAT) null else throw PluginNativeException(PluginFileErrors.NOT_FOUND, "Entry not found")
            2 -> denied()
            3 -> throw PluginNativeException(PluginFileErrors.TYPE_MISMATCH, "Unexpected entry type")
            4 -> throw PluginNativeException(PluginFileErrors.INVALID_MODIFICATION, "Cannot modify this entry")
            else -> throw PluginNativeException(PluginErrors.UNKNOWN, "File operation failed")
        }
    }

    private fun entry(buffer: ByteBuffer, rootName: String = ""): FileEntryInfo? {
        val kind = if (buffer.get().toInt() == 1) FileEntryKind.Directory else FileEntryKind.File
        val size = buffer.long
        val modified = buffer.long
        val nameSize = buffer.int
        require(nameSize in 0..buffer.remaining())
        val bytes = ByteArray(nameSize).also { buffer.get(it) }
        // Linux names may contain arbitrary bytes. Only UTF-8 names can round-trip through the JS API.
        val name = try {
            bytes.decodeToString(throwOnInvalidSequence = true)
        } catch (_: CharacterCodingException) {
            return null
        }
        return FileEntryInfo(name.ifEmpty { rootName }, kind, size, modified)
    }

    override suspend fun stat(scope: String, path: List<String>): FileEntryInfo? =
        request(STAT, scope, path)?.let { entry(it, pathForScope(scope)?.substringAfterLast('/')?.ifEmpty { "/" }.orEmpty()) }

    override suspend fun list(scope: String, path: List<String>): List<FileEntryInfo> {
        val buffer = checkNotNull(request(LIST, scope, path))
        return buildList { while (buffer.hasRemaining()) entry(buffer)?.let { add(it) } }
    }

    override suspend fun read(scope: String, path: List<String>): ByteArray {
        val buffer = checkNotNull(request(READ, scope, path))
        return ByteArray(buffer.remaining()).also { buffer.get(it) }
    }

    override suspend fun write(scope: String, path: List<String>, data: ByteArray, create: Boolean) {
        request(WRITE, scope, path, data, create)
    }

    override suspend fun createDirectory(scope: String, path: List<String>) { request(MKDIR, scope, path) }

    override suspend fun remove(scope: String, path: List<String>, recursive: Boolean) {
        request(REMOVE, scope, path, flag = recursive)
    }

    private companion object {
        val scopeId = Regex("[0-9a-f]{64}")
        const val PROBE = 0
        const val STAT = 1
        const val LIST = 2
        const val READ = 3
        const val WRITE = 4
        const val MKDIR = 5
        const val REMOVE = 6

        fun denied(): Nothing = throw PluginNativeException(PluginErrors.PERMISSION_DENIED, "Folder access is unavailable")
    }
}

internal object PlatformFilesNative {
    @JvmStatic
    external fun request(operation: Int, root: ByteArray, device: Long, inode: Long,
                         parts: Array<ByteArray>, payload: ByteArray?, flag: Boolean): ByteArray
}
