/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.locker.AppPlatform
import io.rebble.libpebblecommon.locker.AppProperties
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.locker.SystemApps
import io.rebble.libpebblecommon.locker.wrap
import io.rebble.libpebblecommon.metadata.WatchType
import org.freedesktop.dbus.types.UInt64
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.attribute.PosixFilePermissions
import java.util.zip.Inflater
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFails
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertTrue
import kotlin.uuid.Uuid

class RockpoolWatchContentTest {
    @Test
    fun `application records preserve the public shape and order`() {
        val later = normalApp(
            uuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
            type = AppType.Watchapp,
            order = 8,
            configurable = true,
            icon = "https://example.test/icon.png",
        )
        val first = systemApp(
            uuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
            order = -2,
        )

        val records = rockpoolApplicationRecords(listOf(later, first), WatchType.BASALT)

        assertEquals(2, records.size)
        assertEquals(first.properties.id.toString(), records[0].getValue("uuid"))
        assertEquals(true, records[0].getValue("systemApp"))
        assertEquals(false, records[0].getValue("hasSettings"))
        assertEquals(AppType.Watchface == first.properties.type, records[0].getValue("watchface"))
        assertEquals(later.properties.id.toString(), records[1].getValue("uuid"))
        assertEquals("store-id", records[1].getValue("storeId"))
        assertEquals("Example", records[1].getValue("name"))
        assertEquals("Developer", records[1].getValue("vendor"))
        assertEquals("1.2.3", records[1].getValue("version"))
        assertEquals(true, records[1].getValue("hasSettings"))
        assertEquals("https://example.test/icon.png", records[1].getValue("icon"))
        assertEquals(false, records[1].getValue("systemApp"))
        assertEquals(
            setOf(
                "uuid",
                "storeId",
                "name",
                "vendor",
                "watchface",
                "version",
                "hasSettings",
                "icon",
                "systemApp",
            ),
            records[1].keys,
        )
    }

    @Test
    fun `application record defaults nullable strings and icon`() {
        val application = normalApp(
            uuid = "cccccccc-cccc-4ccc-8ccc-cccccccccccc",
            type = AppType.Watchface,
            order = 0,
            configurable = false,
            icon = null,
            storeId = null,
            version = null,
        )

        val record = rockpoolApplicationRecords(listOf(application), WatchType.BASALT).single()

        assertEquals("", record.getValue("storeId"))
        assertEquals("", record.getValue("version"))
        assertEquals("", record.getValue("icon"))
        assertEquals(true, record.getValue("watchface"))
    }

    @Test
    fun `application records are scoped to the watch model and sync selection`() {
        val synced = normalApp(
            uuid = "11111111-1111-4111-8111-111111111111",
            type = AppType.Watchapp,
            order = 2,
            configurable = false,
            icon = null,
            platforms = listOf(AppPlatform(WatchType.APLITE)),
        )
        val outsideSyncLimit = normalApp(
            uuid = "22222222-2222-4222-8222-222222222222",
            type = AppType.Watchapp,
            order = 50,
            configurable = false,
            icon = null,
            sync = false,
            platforms = listOf(AppPlatform(WatchType.APLITE)),
        )
        val basaltOnly = normalApp(
            uuid = "33333333-3333-4333-8333-333333333333",
            type = AppType.Watchapp,
            order = 3,
            configurable = false,
            icon = null,
            platforms = listOf(AppPlatform(WatchType.BASALT)),
        )
        val settings = SystemApps.Settings.wrap(order = -10)
        val health = SystemApps.Health.wrap(order = -5)
        val locker = listOf(synced, outsideSyncLimit, basaltOnly, settings, health)

        val aplite = rockpoolApplicationRecords(locker, WatchType.APLITE)
        val basalt = rockpoolApplicationRecords(locker, WatchType.BASALT)

        assertEquals(
            listOf(settings.properties.id, synced.properties.id).map { it.toString() },
            aplite.map { it.getValue("uuid") },
        )
        assertEquals(
            listOf(settings.properties.id, health.properties.id, synced.properties.id, basaltOnly.properties.id)
                .map { it.toString() },
            basalt.map { it.getValue("uuid") },
        )
    }

    @Test
    fun `application records use the selected watch platform icon`() {
        val application = normalApp(
            uuid = "44444444-4444-4444-8444-444444444444",
            type = AppType.Watchface,
            order = 0,
            configurable = false,
            icon = null,
            platforms = listOf(
                AppPlatform(WatchType.APLITE, iconImageUrl = "https://example.test/aplite.png"),
                AppPlatform(WatchType.EMERY, iconImageUrl = "https://example.test/emery.png"),
            ),
        )

        val record = rockpoolApplicationRecords(listOf(application), WatchType.EMERY).single()

        assertEquals("https://example.test/emery.png", record.getValue("icon"))
    }

    @Test
    fun `removal eligibility uses the watch projection`() {
        val synced = normalApp(
            uuid = "55555555-5555-4555-8555-555555555555",
            type = AppType.Watchapp,
            order = 0,
            configurable = false,
            icon = null,
            platforms = listOf(AppPlatform(WatchType.APLITE)),
        )
        val accountOnly = normalApp(
            uuid = "66666666-6666-4666-8666-666666666666",
            type = AppType.Watchapp,
            order = 50,
            configurable = false,
            icon = null,
            sync = false,
            platforms = listOf(AppPlatform(WatchType.APLITE)),
        )
        val incompatible = normalApp(
            uuid = "77777777-7777-4777-8777-777777777777",
            type = AppType.Watchapp,
            order = 1,
            configurable = false,
            icon = null,
            platforms = listOf(AppPlatform(WatchType.CHALK)),
        )

        assertEquals(
            RockpoolApplicationRemovalEligibility.REMOVABLE,
            rockpoolApplicationRemovalEligibility(synced, WatchType.APLITE),
        )
        assertEquals(
            RockpoolApplicationRemovalEligibility.NOT_INSTALLED,
            rockpoolApplicationRemovalEligibility(accountOnly, WatchType.APLITE),
        )
        assertEquals(
            RockpoolApplicationRemovalEligibility.NOT_INSTALLED,
            rockpoolApplicationRemovalEligibility(incompatible, WatchType.APLITE),
        )
        assertEquals(
            RockpoolApplicationRemovalEligibility.SYSTEM,
            rockpoolApplicationRemovalEligibility(SystemApps.Settings.wrap(0), WatchType.APLITE),
        )
        assertEquals(
            RockpoolApplicationRemovalEligibility.NOT_INSTALLED,
            rockpoolApplicationRemovalEligibility(SystemApps.Health.wrap(0), WatchType.APLITE),
        )
    }

    @Test
    fun `only canonical application UUIDs are accepted`() {
        val canonical = "12345678-1234-4abc-8def-1234567890ab"

        assertEquals(canonical, parseCanonicalApplicationUuid(canonical)?.toString())
        assertEquals(canonical, parseCanonicalApplicationUuid(canonical.uppercase())?.toString())
        assertEquals(null, parseCanonicalApplicationUuid("{${canonical}}"))
        assertEquals(null, parseCanonicalApplicationUuid(canonical.replace("-", "")))
        assertEquals(null, parseCanonicalApplicationUuid("not-a-uuid"))
    }

    @Test
    fun `PNG encoder writes exact dimensions and RGBA bytes`() {
        val png = encodeRockpoolPngRgba(
            width = 2,
            height = 1,
            argb = intArrayOf(0x7f112233, 0xffaabbcc.toInt()),
        )

        assertContentEquals(PNG_SIGNATURE, png.copyOfRange(0, PNG_SIGNATURE.size))
        val chunks = pngChunks(png)
        val header = assertNotNull(chunks["IHDR"]).single()
        assertEquals(2, header.readInt(0))
        assertEquals(1, header.readInt(4))
        assertEquals(8, header[8].toUByte().toInt())
        assertEquals(6, header[9].toUByte().toInt())
        assertContentEquals(
            byteArrayOf(0, 0x11, 0x22, 0x33, 0x7f, 0xaa.toByte(), 0xbb.toByte(), 0xcc.toByte(), 0xff.toByte()),
            inflate(assertNotNull(chunks["IDAT"]).reduce { left, right -> left + right }),
        )
        assertTrue(chunks.containsKey("IEND"))
    }

    @Test
    fun `screenshot dimensions and buffers are bounded`() {
        assertFails { rockpoolScreenshotPixelCount(0, 1) }
        assertFails { rockpoolScreenshotPixelCount(1, 0) }
        assertFails { rockpoolScreenshotPixelCount(2049, 1) }
        assertFails { rockpoolScreenshotPixelCount(2048, 2048 + 1) }
        assertFails { encodeRockpoolPngRgba(2, 2, IntArray(3)) }
        assertEquals(4_194_304, rockpoolScreenshotPixelCount(2048, 2048))
    }

    @Test
    fun `screenshot store atomically writes and lists typed records`() {
        val home = Files.createTempDirectory("rockpool-screenshot-test-")
        try {
            val store = RockpoolScreenshotStore(
                home = home,
                nowMillis = { 1_725_000_000_123L },
                randomId = { "0123456789abcdef0123456789abcdef" },
            )
            assertEquals(emptyList(), store.list())

            val written = store.write(encodeRockpoolPngRgba(1, 1, intArrayOf(0xff102030.toInt())))
            val path = home.resolve("Pictures/Screenshots/Pebble").resolve(written.id)

            assertEquals("pebble-1725000000123-0123456789abcdef0123456789abcdef.png", written.id)
            assertEquals(path.toString(), written.path)
            assertTrue(Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS))
            assertEquals(
                PosixFilePermissions.fromString("rw-------"),
                Files.getPosixFilePermissions(path, LinkOption.NOFOLLOW_LINKS),
            )
            assertEquals(listOf(written), store.list())
            val variantRecord = written.asVariantMap()
            assertEquals("image/png", variantRecord.getValue("mime"))
            assertEquals(1_725_000_000_123L, assertIs<UInt64>(variantRecord.getValue("created")).toLong())
        } finally {
            deleteTree(home)
        }
    }

    @Test
    fun `screenshot store removes only a canonical regular screenshot`() {
        val home = Files.createTempDirectory("rockpool-screenshot-remove-")
        val outside = Files.createTempDirectory("rockpool-screenshot-remove-outside-")
        try {
            val store = RockpoolScreenshotStore(
                home = home,
                nowMillis = { 1_725_000_000_123L },
                randomId = { "0123456789abcdef0123456789abcdef" },
            )
            val written = store.write(
                encodeRockpoolPngRgba(1, 1, intArrayOf(0xff102030.toInt())),
            )
            val directory = home.resolve("Pictures/Screenshots/Pebble")
            val outsideFile = outside.resolve("pebble-1725000000999.png")
            Files.write(outsideFile, byteArrayOf(1))
            val link = directory.resolve("pebble-1725000000888.png")
            Files.createSymbolicLink(link, outsideFile)

            assertEquals(null, store.remove("../${written.id}"))
            assertEquals(null, store.remove(outsideFile.toString()))
            assertEquals(null, store.remove(link.toString()))
            assertTrue(Files.isSymbolicLink(link))
            assertTrue(Files.exists(outsideFile))

            assertEquals(written, store.remove(written.path))
            assertEquals(emptyList(), store.list())
            assertEquals(null, store.remove(written.path))
        } finally {
            deleteTree(home)
            deleteTree(outside)
        }
    }

    @Test
    fun `screenshot listing is deterministic and excludes unsafe entries`() {
        val home = Files.createTempDirectory("rockpool-screenshot-list-")
        try {
            val directory = Files.createDirectories(home.resolve("Pictures/Screenshots/Pebble"))
            generateSequence(directory) { path ->
                path.parent?.takeIf { it.startsWith(home) }
            }.forEach { path ->
                Files.setPosixFilePermissions(path, PosixFilePermissions.fromString("rwx------"))
            }
            val old = directory.resolve("pebble-1725000000000.png")
            val newer = directory.resolve("pebble-1725000001000-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.png")
            Files.write(old, encodeRockpoolPngRgba(1, 1, intArrayOf(0xff000000.toInt())))
            Files.write(newer, encodeRockpoolPngRgba(1, 1, intArrayOf(0xffffffff.toInt())))
            Files.write(directory.resolve("unrelated.png"), byteArrayOf(1))
            Files.createDirectory(directory.resolve("pebble-1725000002000.png"))
            Files.createSymbolicLink(
                directory.resolve("pebble-1725000003000.png"),
                newer.fileName,
            )

            val records = RockpoolScreenshotStore(home).list()

            assertEquals(listOf(newer.fileName.toString(), old.fileName.toString()), records.map { it.id })
        } finally {
            deleteTree(home)
        }
    }

    @Test
    fun `screenshot store rejects a symlinked directory component`() {
        val home = Files.createTempDirectory("rockpool-screenshot-link-")
        val outside = Files.createTempDirectory("rockpool-screenshot-outside-")
        try {
            Files.createSymbolicLink(home.resolve("Pictures"), outside)
            val store = RockpoolScreenshotStore(home)

            assertFails { store.list() }
            assertFails {
                store.write(encodeRockpoolPngRgba(1, 1, intArrayOf(0xff000000.toInt())))
            }
            assertEquals(emptyList(), Files.list(outside).use { it.toList() })
        } finally {
            deleteTree(home)
            deleteTree(outside)
        }
    }

    @Test
    fun `cancel before screenshot commit removes the temporary file`() {
        val home = Files.createTempDirectory("rockpool-screenshot-cancel-")
        try {
            val store = RockpoolScreenshotStore(
                home = home,
                nowMillis = { 1_725_000_000_123L },
                randomId = { "fedcba9876543210fedcba9876543210" },
            )

            assertFails {
                store.write(
                    encodeRockpoolPngRgba(1, 1, intArrayOf(0xff000000.toInt())),
                    beginCommit = { false },
                )
            }
            val directory = home.resolve("Pictures/Screenshots/Pebble")
            assertEquals(emptyList(), Files.list(directory).use { it.toList() })
            assertEquals(emptyList(), store.list())
        } finally {
            deleteTree(home)
        }
    }

    private fun normalApp(
        uuid: String,
        type: AppType,
        order: Int,
        configurable: Boolean,
        icon: String?,
        storeId: String? = "store-id",
        version: String? = "1.2.3",
        sync: Boolean = true,
        platforms: List<AppPlatform> = listOf(AppPlatform(WatchType.BASALT, iconImageUrl = icon)),
    ): LockerWrapper.NormalApp = LockerWrapper.NormalApp(
        properties = properties(uuid, type, order, storeId, version, platforms),
        sideloaded = true,
        configurable = configurable,
        sync = sync,
    )

    private fun systemApp(uuid: String, order: Int): LockerWrapper.SystemApp = LockerWrapper.SystemApp(
        properties = properties(
            uuid,
            AppType.Watchapp,
            order,
            null,
            null,
            listOf(AppPlatform(WatchType.BASALT)),
        ),
        systemApp = SystemApps.Settings,
    )

    private fun properties(
        uuid: String,
        type: AppType,
        order: Int,
        storeId: String?,
        version: String?,
        platforms: List<AppPlatform>,
    ) = AppProperties(
        id = Uuid.parse(uuid),
        type = type,
        title = "Example",
        developerName = "Developer",
        developerId = "developer-id",
        platforms = platforms,
        version = version,
        hearts = null,
        category = null,
        iosCompanion = null,
        androidCompanion = null,
        order = order,
        sourceLink = null,
        storeId = storeId,
        capabilities = emptyList(),
    )

    private fun Map<String, org.freedesktop.dbus.types.Variant<*>>.getValue(key: String): Any? =
        checkNotNull(this[key]).value

    private fun pngChunks(png: ByteArray): Map<String, List<ByteArray>> {
        var offset = PNG_SIGNATURE.size
        val chunks = linkedMapOf<String, MutableList<ByteArray>>()
        while (offset < png.size) {
            val length = png.readInt(offset)
            val type = png.copyOfRange(offset + 4, offset + 8).toString(Charsets.US_ASCII)
            val dataStart = offset + 8
            val dataEnd = dataStart + length
            chunks.getOrPut(type, ::mutableListOf) += png.copyOfRange(dataStart, dataEnd)
            offset = dataEnd + 4
        }
        return chunks
    }

    private fun inflate(compressed: ByteArray): ByteArray {
        val inflater = Inflater().apply { setInput(compressed) }
        return try {
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(1024)
            while (!inflater.finished()) {
                val count = inflater.inflate(buffer)
                assertTrue(count > 0 || inflater.finished())
                output.write(buffer, 0, count)
            }
            output.toByteArray()
        } finally {
            inflater.end()
        }
    }

    private fun ByteArray.readInt(offset: Int): Int =
        ByteBuffer.wrap(this, offset, Int.SIZE_BYTES).order(ByteOrder.BIG_ENDIAN).int

    private fun deleteTree(root: java.nio.file.Path) {
        if (!Files.exists(root, LinkOption.NOFOLLOW_LINKS)) return
        Files.walk(root).use { paths ->
            paths.sorted(Comparator.reverseOrder()).forEach(Files::deleteIfExists)
        }
    }

    companion object {
        private val PNG_SIGNATURE = byteArrayOf(
            0x89.toByte(), 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        )
    }
}
