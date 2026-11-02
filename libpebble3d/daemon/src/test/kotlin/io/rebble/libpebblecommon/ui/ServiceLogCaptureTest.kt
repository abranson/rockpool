package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import co.touchlab.kermit.Severity
import co.touchlab.kermit.StaticConfig
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ServiceLogCaptureTest {
    @Test
    fun keepsRecentEntriesWithinBoundAndReportsEviction() {
        val logs = RecentServiceLogs(256)
        repeat(100) { logs.log(Severity.Info, "message-$it", "test", null) }
        val snapshot = logs.snapshot()
        assertTrue(snapshot.contains("Older entries discarded"))
        assertTrue(snapshot.contains("message-99"))
        assertFalse(snapshot.contains("message-0\n"))
        assertTrue(snapshot.substringAfter("# Older entries discarded because the buffer is full.\n").length <= 256)
    }

    @Test
    fun boundsIndividualMessagesAndRetainsExceptionContext() {
        val logs = RecentServiceLogs()
        logs.log(Severity.Info, "x".repeat(100_000), "test", null)
        assertTrue(logs.snapshot().length < 17_000)
        logs.log(Severity.Error, "failed", "test", IllegalStateException("test failure"))
        assertTrue(logs.snapshot().contains("IllegalStateException: test failure"))
    }

    @Test
    fun loggerUsesCurrentSeverityWithoutExportChangingIt() {
        val logs = RecentServiceLogs()
        val logger = Logger(StaticConfig(Severity.Info, listOf(logs)), "test")
        logger.d { "hidden" }
        logger.i { "visible" }
        assertFalse(logs.snapshot().contains("hidden"))
        assertTrue(logs.snapshot().contains("visible"))
    }

    @Test
    fun exportIsPrivateAndAtomicallyReplacesSymlinkWithoutFollowingIt() {
        val home = Files.createTempDirectory("rockpool-log-export")
        try {
            val outside = home.resolve("preserve")
            Files.write(outside, "untouched".toByteArray())
            Files.createDirectory(home.resolve("Downloads"))
            val target = home.resolve("Downloads/libpebble3d.log")
            Files.createSymbolicLink(target, outside)
            assertEquals(target, writeLogExport("libpebble3d.log", home) { it.write("logs") })
            assertEquals("logs", String(Files.readAllBytes(target)))
            assertEquals("untouched", String(Files.readAllBytes(outside)))
            assertEquals(PosixFilePermissions.fromString("rw-------"), Files.getPosixFilePermissions(target))
        } finally {
            home.toFile().deleteRecursively()
        }
    }

    @Test
    fun failedExportPreservesPreviousSnapshotAndCleansTemporaryFile() {
        val home = Files.createTempDirectory("rockpool-log-export")
        try {
            val target = writeLogExport("libpebble3d.log", home) { it.write("previous") }
            assertFailsWith<IllegalStateException> {
                writeLogExport("libpebble3d.log", home) { it.write("partial"); error("failed") }
            }
            assertEquals("previous", String(Files.readAllBytes(target)))
            Files.list(target.parent).use { assertEquals(1L, it.count()) }
        } finally {
            home.toFile().deleteRecursively()
        }
    }

    @Test
    fun rejectsSymlinkedDownloadsAndUnrecognizedFileNames() {
        val home = Files.createTempDirectory("rockpool-log-export")
        val outside = Files.createTempDirectory("rockpool-log-outside")
        try {
            Files.createSymbolicLink(home.resolve("Downloads"), outside)
            assertFailsWith<Exception> { writeLogExport("libpebble3d.log", home) { it.write("no") } }
            assertFailsWith<IllegalArgumentException> { writeLogExport("../escape", home) { it.write("no") } }
            Files.list(outside).use { assertEquals(0L, it.count()) }
        } finally {
            Files.deleteIfExists(home.resolve("Downloads"))
            home.toFile().deleteRecursively()
            outside.toFile().deleteRecursively()
        }
    }
}
