/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlin.uuid.Uuid

/** Publishes per-watch application run-state changes without retaining a retired connection. */
internal class RunningAppStateObserver(
    private val scope: CoroutineScope,
    private val emit: () -> Unit,
) {
    private val lock = Any()
    private var generation = 0L
    private var source: Any? = null
    private var runningApp: Uuid? = null
    private var collector: Job? = null

    fun update(source: Any?, state: StateFlow<Uuid?>?) {
        require((source == null) == (state == null)) { "source and state must change together" }

        val previousCollector: Job?
        val selectedGeneration: Long
        val selectedValue = state?.value
        val sourceChanged: Boolean
        synchronized(lock) {
            if (this.source === source) return
            generation += 1
            selectedGeneration = generation
            this.source = source
            previousCollector = collector
            collector = null
            sourceChanged = runningApp != selectedValue
            runningApp = selectedValue
        }
        previousCollector?.cancel()
        if (sourceChanged) emit()
        if (source == null || state == null) return

        val nextCollector = scope.launch {
            state.collect { value ->
                val changed = synchronized(lock) {
                    if (
                        generation != selectedGeneration ||
                        this@RunningAppStateObserver.source !== source ||
                        runningApp == value
                    ) {
                        false
                    } else {
                        runningApp = value
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
