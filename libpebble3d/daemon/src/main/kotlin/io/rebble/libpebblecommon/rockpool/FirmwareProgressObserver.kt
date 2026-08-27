/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/** Observes the nested updater progress flow while rejecting a retired update session. */
internal class FirmwareProgressObserver(
    private val scope: CoroutineScope,
    private val emit: () -> Unit,
) {
    private val lock = Any()
    private var generation = 0L
    private var source: Any? = null
    private var progress = 0.0f
    private var collector: Job? = null

    fun update(source: Any?, state: StateFlow<Float>?) {
        require((source == null) == (state == null)) { "source and state must change together" }
        val previousCollector: Job?
        val selectedGeneration: Long
        synchronized(lock) {
            if (this.source === source) return
            generation += 1
            selectedGeneration = generation
            this.source = source
            progress = state?.value ?: 0.0f
            previousCollector = collector
            collector = null
        }
        previousCollector?.cancel()
        if (source == null || state == null) return

        val nextCollector = scope.launch {
            state.collect { value ->
                val changed = synchronized(lock) {
                    if (
                        generation != selectedGeneration ||
                        this@FirmwareProgressObserver.source !== source ||
                        progress == value
                    ) {
                        false
                    } else {
                        progress = value
                        true
                    }
                }
                if (changed) emit()
            }
        }
        synchronized(lock) {
            if (generation == selectedGeneration && this.source === source) {
                collector = nextCollector
            } else {
                nextCollector.cancel()
            }
        }
    }

    fun close() = update(null, null)
}
