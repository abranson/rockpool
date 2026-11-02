package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.LogWriter
import co.touchlab.kermit.Severity
import java.time.Instant
import java.util.ArrayDeque

// Capture starts with the daemon and is bounded independently of debug verbosity.
// Nothing is written to disk until the user requests a snapshot.
internal class RecentServiceLogs(private val maxChars: Int = 512 * 1024) : LogWriter() {
    private val entries = ArrayDeque<String>()
    private var chars = 0
    private var discarded = false

    init {
        require(maxChars > 0)
    }

    override fun log(severity: Severity, message: String, tag: String, throwable: Throwable?) {
        val limit = minOf(maxChars, 16 * 1024)
        val entry = buildString {
            append(Instant.now()).append(' ').append(severity.name)
            append(' ').append(tag.take(128)).append(": ")
            append(message.take(limit))
            if (throwable != null) {
                append('\n').append(throwable.toString().take(1024))
                throwable.stackTrace.take(32).forEach { append("\n    at ").append(it) }
            }
        }
        append(if (entry.length >= limit) entry.take(limit - 1) + "\n" else entry + "\n")
    }

    @Synchronized
    private fun append(entry: String) {
        while (chars + entry.length > maxChars) {
            chars -= entries.removeFirst().length
            discarded = true
        }
        entries.addLast(entry)
        chars += entry.length
    }

    @Synchronized
    fun snapshot(): String = buildString {
        append("# libpebble3d recent logs (current daemon session)\n")
        if (discarded) append("# Older entries discarded because the buffer is full.\n")
        if (entries.isEmpty()) append("# No service log messages captured.\n")
        entries.forEach { append(it) }
    }
}

internal object ServiceLogCapture {
    val writer = RecentServiceLogs()
    fun snapshot(): String = writer.snapshot()
}
