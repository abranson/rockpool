package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.storage.SecureStorage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.IOException
import java.security.MessageDigest
import kotlin.uuid.Uuid

internal class SailfishSecureStorage(pluginUuid: Uuid) : SecureStorage {
    private val uuid = pluginUuid.toString()

    override suspend fun get(key: String): String? =
        execute(GET, key)?.let { result ->
            try {
                result.decodeToString(startIndex = 1, throwOnInvalidSequence = true)
            } finally {
                result.fill(0)
            }
        }

    override suspend fun set(key: String, value: String) {
        val bytes = value.encodeToByteArray()
        try {
            execute(SET, key, bytes)
        } finally {
            bytes.fill(0)
        }
    }

    override suspend fun remove(key: String) {
        execute(REMOVE, key)
    }

    override suspend fun destroy() {
        execute(DESTROY)
    }

    private suspend fun execute(operation: Int, key: String? = null, data: ByteArray? = null): ByteArray? =
        withContext(dispatcher) {
            val identifier = key?.let { MessageDigest.getInstance("SHA-256").digest(it.encodeToByteArray()).toHexString() }
            if (!PlatformProviderController.nativeLibraryAvailable()) {
                throw IOException("Platform secure storage is unavailable")
            }
            val result = PlatformProviderNative.secureStorage(operation, uuid, identifier, data)
                ?: throw IOException("Secure storage operation failed")
            when (result.first().toInt()) {
                ABSENT -> null
                else -> result
            }
        }

    private companion object {
        const val GET = 0
        const val SET = 1
        const val REMOVE = 2
        const val DESTROY = 3

        const val ABSENT = 1

        // Serialise writes, removal and collection destruction across instances for the same plugin.
        val dispatcher = Dispatchers.IO.limitedParallelism(1)
    }
}
