/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.locker.findCompatiblePlatform
import io.rebble.libpebblecommon.metadata.WatchType
import org.freedesktop.dbus.types.UInt64
import org.freedesktop.dbus.types.Variant
import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.nio.channels.Channels
import java.nio.file.DirectoryStream
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.NoSuchFileException
import java.nio.file.OpenOption
import java.nio.file.Path
import java.nio.file.Paths
import java.nio.file.SecureDirectoryStream
import java.nio.file.StandardOpenOption
import java.nio.file.attribute.PosixFilePermission
import java.nio.file.attribute.BasicFileAttributeView
import java.nio.file.attribute.PosixFilePermissions
import java.util.UUID
import java.util.zip.CRC32
import java.util.zip.Deflater
import kotlin.uuid.Uuid

internal fun rockpoolApplicationRecords(
    applications: List<LockerWrapper>,
    watchType: WatchType,
    runningApp: Uuid? = null,
): List<Map<String, Variant<*>>> = applications
    .filter { rockpoolApplicationIsInstalledOnWatch(it, watchType) }
    .sortedBy { it.properties.order }
    .map { app ->
        val properties = app.properties
        val platform = checkNotNull(app.findCompatiblePlatform(watchType))
        linkedMapOf(
            "uuid" to Variant(properties.id.toString()),
            "storeId" to Variant(properties.storeId.orEmpty()),
            "name" to Variant(properties.title),
            "vendor" to Variant(properties.developerName),
            "watchface" to Variant(properties.type == AppType.Watchface),
            "version" to Variant(properties.version.orEmpty()),
            "hasSettings" to Variant((app as? LockerWrapper.NormalApp)?.configurable ?: false),
            "icon" to Variant(platform.iconImageUrl.orEmpty()),
            "systemApp" to Variant(app is LockerWrapper.SystemApp),
            "running" to Variant(properties.id == runningApp),
        )
    }

internal fun rockpoolApplicationIsInstalledOnWatch(
    application: LockerWrapper,
    watchType: WatchType,
): Boolean = application.findCompatiblePlatform(watchType) != null && when (application) {
    is LockerWrapper.NormalApp -> application.sync
    is LockerWrapper.SystemApp -> true
}

internal enum class RockpoolApplicationRemovalEligibility {
    REMOVABLE,
    NOT_INSTALLED,
    SYSTEM,
}

internal fun rockpoolApplicationRemovalEligibility(
    application: LockerWrapper,
    watchType: WatchType,
): RockpoolApplicationRemovalEligibility = when {
    !rockpoolApplicationIsInstalledOnWatch(application, watchType) ->
        RockpoolApplicationRemovalEligibility.NOT_INSTALLED
    application is LockerWrapper.SystemApp -> RockpoolApplicationRemovalEligibility.SYSTEM
    else -> RockpoolApplicationRemovalEligibility.REMOVABLE
}

/** The primary API accepts UUIDs, not the compatibility API's store IDs or braced UUIDs. */
internal fun parseCanonicalApplicationUuid(value: String): Uuid? =
    runCatching { Uuid.parse(value) }.getOrNull()
        ?.takeIf { it.toString().equals(value, ignoreCase = true) }

internal fun validApplicationConfigurationUrl(value: String): Boolean =
    value.isNotBlank() && '\u0000' !in value &&
        value.encodeToByteArray().size <= MAX_APPLICATION_CONFIGURATION_URL_BYTES

internal fun validApplicationConfigurationResult(value: String): Boolean =
    '\u0000' !in value &&
        value.encodeToByteArray().size <= MAX_APPLICATION_CONFIGURATION_RESULT_BYTES

internal data class RockpoolScreenshotRecord(
    val id: String,
    val path: String,
    val createdMillis: Long,
) {
    init {
        require(createdMillis >= 0)
    }

    fun asVariantMap(): Map<String, Variant<*>> = linkedMapOf(
        "id" to Variant(id),
        "path" to Variant(path),
        "mime" to Variant(PNG_MIME_TYPE),
        "created" to Variant(UInt64(createdMillis)),
    )
}

/**
 * Fixed screenshot storage for the primary API.
 *
 * Every directory component is opened through [SecureDirectoryStream] without following links,
 * and files are created under a random temporary name before an in-directory atomic rename.
 */
internal class RockpoolScreenshotStore(
    private val home: Path,
    private val nowMillis: () -> Long = System::currentTimeMillis,
    private val randomId: () -> String = {
        UUID.randomUUID().toString().replace("-", "")
    },
) {
    init {
        require(home.isAbsolute) { "user home must be absolute" }
    }

    fun list(): List<RockpoolScreenshotRecord> {
        val opened = openScreenshotDirectory(create = false) ?: return emptyList()
        opened.use { directory ->
            return directory.stream.mapNotNull { entry ->
                val id = entry.fileName.toString()
                val match = SCREENSHOT_NAME.matchEntire(id) ?: return@mapNotNull null
                val created = match.groupValues[1].toLongOrNull() ?: return@mapNotNull null
                val attributes = directory.stream
                    .getFileAttributeView(
                        Paths.get(id),
                        BasicFileAttributeView::class.java,
                        LinkOption.NOFOLLOW_LINKS,
                    )
                    ?.readAttributes()
                    ?: return@mapNotNull null
                if (!attributes.isRegularFile || attributes.isSymbolicLink) {
                    return@mapNotNull null
                }
                RockpoolScreenshotRecord(
                    id = id,
                    path = directory.path.resolve(id).toString(),
                    createdMillis = created,
                )
            }.sortedWith(
                compareByDescending<RockpoolScreenshotRecord> { it.createdMillis }
                    .thenByDescending { it.id },
            )
        }
    }

    fun write(
        png: ByteArray,
        beginCommit: () -> Boolean = { true },
    ): RockpoolScreenshotRecord {
        require(png.size in PNG_SIGNATURE.size..MAX_PNG_BYTES) { "invalid screenshot size" }
        require(png.copyOfRange(0, PNG_SIGNATURE.size).contentEquals(PNG_SIGNATURE)) {
            "invalid screenshot encoding"
        }
        val created = nowMillis()
        require(created >= 0) { "invalid screenshot timestamp" }
        val suffix = randomId()
        require(RANDOM_ID.matches(suffix)) { "invalid screenshot identifier" }
        val id = "pebble-$created-$suffix.png"
        val temporaryId = ".$id.tmp"

        val opened = checkNotNull(openScreenshotDirectory(create = true))
        opened.use { directory ->
            val temporary = Paths.get(temporaryId)
            var temporaryExists = false
            try {
                val options = setOf<OpenOption>(
                    StandardOpenOption.CREATE_NEW,
                    StandardOpenOption.WRITE,
                    LinkOption.NOFOLLOW_LINKS,
                )
                directory.stream.newByteChannel(
                    temporary,
                    options,
                    PosixFilePermissions.asFileAttribute(FILE_PERMISSIONS),
                ).use { channel ->
                    temporaryExists = true
                    Channels.newOutputStream(channel).use { output -> output.write(png) }
                }
                if (!beginCommit()) throw java.util.concurrent.CancellationException()
                directory.stream.move(temporary, directory.stream, Paths.get(id))
                temporaryExists = false
            } finally {
                if (temporaryExists) runCatching { directory.stream.deleteFile(temporary) }
            }
            return RockpoolScreenshotRecord(
                id = id,
                path = directory.path.resolve(id).toString(),
                createdMillis = created,
            )
        }
    }

    /**
     * Remove one screenshot previously returned by [list] or [write].
     *
     * The caller-visible absolute path is accepted for compatibility with org.rockpool, but the
     * deletion itself is resolved through the already-open secure directory and never follows a
     * caller-selected directory or symbolic link.
     */
    fun remove(path: String): RockpoolScreenshotRecord? {
        val requested = runCatching { Paths.get(path) }.getOrNull()
            ?.takeIf { it.isAbsolute }
            ?: return null
        val id = requested.fileName?.toString() ?: return null
        val match = SCREENSHOT_NAME.matchEntire(id) ?: return null
        val created = match.groupValues[1].toLongOrNull() ?: return null
        val opened = openScreenshotDirectory(create = false) ?: return null
        opened.use { directory ->
            val canonical = directory.path.resolve(id)
            if (requested.toString() != canonical.toString()) return null
            val relative = Paths.get(id)
            val attributes = try {
                directory.stream.getFileAttributeView(
                    relative,
                    BasicFileAttributeView::class.java,
                    LinkOption.NOFOLLOW_LINKS,
                )?.readAttributes()
            } catch (_: NoSuchFileException) {
                null
            } ?: return null
            if (!attributes.isRegularFile || attributes.isSymbolicLink) return null
            try {
                directory.stream.deleteFile(relative)
            } catch (_: NoSuchFileException) {
                return null
            }
            return RockpoolScreenshotRecord(id, canonical.toString(), created)
        }
    }

    private fun openScreenshotDirectory(create: Boolean): OpenDirectory? {
        require(Files.isDirectory(home, LinkOption.NOFOLLOW_LINKS) && !Files.isSymbolicLink(home)) {
            "user home is unavailable"
        }
        val streams = mutableListOf<DirectoryStream<Path>>()
        try {
            val homeStream = Files.newDirectoryStream(home)
            streams += homeStream
            var secure = homeStream as? SecureDirectoryStream<Path>
                ?: error("secure directory access is unavailable")
            var current = home

            for (component in SCREENSHOT_DIRECTORY_COMPONENTS) {
                val relative = Paths.get(component)
                val childPath = current.resolve(relative)
                val child = try {
                    secure.newDirectoryStream(relative, LinkOption.NOFOLLOW_LINKS)
                } catch (_: NoSuchFileException) {
                    if (!create) {
                        streams.asReversed().forEach { runCatching { it.close() } }
                        return null
                    }
                    try {
                        Files.createDirectory(
                            childPath,
                            PosixFilePermissions.asFileAttribute(DIRECTORY_PERMISSIONS),
                        )
                    } catch (_: FileAlreadyExistsException) {
                        // A concurrent creator won. The secure open below validates its entry.
                    }
                    secure.newDirectoryStream(relative, LinkOption.NOFOLLOW_LINKS)
                }
                streams += child
                secure = child as? SecureDirectoryStream<Path>
                    ?: error("secure directory access is unavailable")
                current = childPath
            }
            return OpenDirectory(current, secure, streams)
        } catch (e: Exception) {
            streams.asReversed().forEach { runCatching { it.close() } }
            throw e
        }
    }

    private class OpenDirectory(
        val path: Path,
        val stream: SecureDirectoryStream<Path>,
        private val streams: List<DirectoryStream<Path>>,
    ) : Closeable {
        override fun close() {
            streams.asReversed().forEach { it.close() }
        }
    }

    companion object {
        fun forCurrentUser(): RockpoolScreenshotStore {
            val home = System.getProperty("user.home")?.takeIf { it.isNotBlank() }
                ?: error("user home directory is unavailable")
            return RockpoolScreenshotStore(Paths.get(home).toAbsolutePath().normalize())
        }

        private const val SAILFISH_PICTURES_DIRECTORY = "Pictures"
        private val SCREENSHOT_DIRECTORY_COMPONENTS =
            listOf(SAILFISH_PICTURES_DIRECTORY, "Screenshots", "Pebble")
        private val SCREENSHOT_NAME = Regex("pebble-(\\d{10,17})(?:-[0-9a-f]{32})?\\.png")
        private val RANDOM_ID = Regex("[0-9a-f]{32}")
        private val DIRECTORY_PERMISSIONS = PosixFilePermissions.fromString("rwx------")
        private val FILE_PERMISSIONS = setOf(
            PosixFilePermission.OWNER_READ,
            PosixFilePermission.OWNER_WRITE,
        )
        private const val MAX_PNG_BYTES = 32 * 1024 * 1024
    }
}

/** Pure-JVM/Graal-safe ARGB_8888 to PNG encoder. */
internal fun encodeRockpoolPngRgba(width: Int, height: Int, argb: IntArray): ByteArray {
    val pixelCount = rockpoolScreenshotPixelCount(width, height)
    require(argb.size == pixelCount) { "invalid screenshot pixel buffer" }
    val rowSize = 1L + width.toLong() * 4L
    val rawSize = rowSize * height.toLong()
    require(rawSize <= MAX_SCREENSHOT_RAW_BYTES) { "screenshot is too large" }

    val raw = ByteArray(rawSize.toInt())
    var outputIndex = 0
    for (y in 0 until height) {
        raw[outputIndex++] = 0
        val rowStart = y * width
        for (x in 0 until width) {
            val color = argb[rowStart + x]
            raw[outputIndex++] = ((color shr 16) and 0xff).toByte()
            raw[outputIndex++] = ((color shr 8) and 0xff).toByte()
            raw[outputIndex++] = (color and 0xff).toByte()
            raw[outputIndex++] = ((color ushr 24) and 0xff).toByte()
        }
    }

    val output = ByteArrayOutputStream()
    output.write(PNG_SIGNATURE)
    val header = ByteArrayOutputStream().apply {
        writeIntBigEndian(width)
        writeIntBigEndian(height)
        write(8)
        write(6)
        write(0)
        write(0)
        write(0)
    }.toByteArray()
    output.writePngChunk("IHDR", header)

    val deflater = Deflater(Deflater.BEST_COMPRESSION).apply {
        setInput(raw)
        finish()
    }
    try {
        val compressed = ByteArrayOutputStream()
        val buffer = ByteArray(64 * 1024)
        while (!deflater.finished()) {
            val count = deflater.deflate(buffer)
            check(count > 0 || deflater.finished()) { "PNG compression stalled" }
            compressed.write(buffer, 0, count)
        }
        output.writePngChunk("IDAT", compressed.toByteArray())
    } finally {
        deflater.end()
    }
    output.writePngChunk("IEND", ByteArray(0))
    return output.toByteArray()
}

internal fun rockpoolScreenshotPixelCount(width: Int, height: Int): Int {
    require(width in 1..MAX_SCREENSHOT_DIMENSION && height in 1..MAX_SCREENSHOT_DIMENSION) {
        "invalid screenshot dimensions"
    }
    val pixelCount = width.toLong() * height.toLong()
    require(pixelCount <= MAX_SCREENSHOT_PIXELS) { "screenshot is too large" }
    return pixelCount.toInt()
}

private fun ByteArrayOutputStream.writeIntBigEndian(value: Int) {
    write((value ushr 24) and 0xff)
    write((value ushr 16) and 0xff)
    write((value ushr 8) and 0xff)
    write(value and 0xff)
}

private fun ByteArrayOutputStream.writePngChunk(type: String, data: ByteArray) {
    writeIntBigEndian(data.size)
    val typeBytes = type.toByteArray(Charsets.US_ASCII)
    write(typeBytes)
    write(data)
    val crc = CRC32().apply {
        update(typeBytes)
        update(data)
    }
    writeIntBigEndian(crc.value.toInt())
}

private val PNG_SIGNATURE = byteArrayOf(
    0x89.toByte(), 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
)
private const val PNG_MIME_TYPE = "image/png"
private const val MAX_APPLICATION_CONFIGURATION_URL_BYTES = 16 * 1024
private const val MAX_APPLICATION_CONFIGURATION_RESULT_BYTES = 64 * 1024
private const val MAX_SCREENSHOT_DIMENSION = 2048
private const val MAX_SCREENSHOT_PIXELS = 4_194_304L
private const val MAX_SCREENSHOT_RAW_BYTES = 17L * 1024L * 1024L
