/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.timeline.TimelineColor
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.CopyOnWriteArrayList

/** Bounded, process-local defaults cache. No values are written to app overrides. */
internal class NotificationIconColors(
    scope: CoroutineScope,
    private val extract: suspend (String, String) -> TimelineColor?,
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
    private val isAvailable: () -> Boolean = { true },
) {
    private class Entry(val source: String, val icon: String, val previous: TimelineColor?) {
        val result = CompletableDeferred<TimelineColor?>()
        var expires = Long.MAX_VALUE
    }
    private val entries = LinkedHashMap<String, Entry>(256, 0.75f, true)
    private val queue = Channel<Entry>(64)
    private val listeners = CopyOnWriteArrayList<() -> Unit>()

    init {
        scope.launch {
            for (entry in queue) {
                val color = try {
                    extract(entry.source, entry.icon)
                } catch (e: CancellationException) {
                    entry.result.cancel(e)
                    throw e
                } catch (_: Exception) {
                    null
                }
                synchronized(entries) {
                    // Recheck unchanged icon files daily; retry missing icons after five minutes.
                    entry.expires = now() + if (color == null) 300_000L else 86_400_000L
                    entry.result.complete(color)
                }
                if (synchronized(entries) { entries[entry.source] === entry }) {
                    listeners.forEach { it() }
                }
            }
        }
    }

    fun addListener(listener: () -> Unit) { listeners.add(listener) }

    fun availabilityChanged() { listeners.forEach { it() } }

    @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
    private fun request(source: String, icon: String): Entry? = synchronized(entries) {
        if (source.isBlank() || source.length > 256 || icon.length > 1024) return null
        if (notificationAppDefaults(source, icon).second.isNotEmpty()) return null
        val old = entries[source]
        // Provider startup is not evidence that the app has no usable icon.
        if (!isAvailable()) return old
        val effectiveIcon = icon.ifEmpty { old?.icon.orEmpty() }
        if (old != null && old.icon == effectiveIcon && old.expires > now()) return old
        val previous = old?.let { if (it.result.isCompleted && !it.result.isCancelled) it.result.getCompleted() else it.previous }
        val entry = Entry(source, effectiveIcon, previous)
        entries[source] = entry
        if (queue.trySend(entry).isFailure) {
            if (old == null) entries.remove(source) else entries[source] = old
            return old
        }
        while (entries.size > 256) entries.remove(entries.keys.first())
        entry
    }

    @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
    fun cached(source: String, icon: String = ""): TimelineColor? {
        val entry = request(source, icon) ?: return null
        return if (entry.result.isCompleted && !entry.result.isCancelled) entry.result.getCompleted() else entry.previous
    }

    suspend fun resolve(source: String, icon: String): TimelineColor? {
        val entry = request(source, icon) ?: return null
        // Never hold up notification delivery behind a cold cache's entire queue.
        return withTimeoutOrNull(1200) { entry.result.await() } ?: entry.previous
    }
}
