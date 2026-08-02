/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Converts a replaceable connected-watch state flow into an exported runtime-state signal.
 *
 * The source token is deliberately the connected device instance rather than its address: a
 * reconnect replaces that instance, and a queued emission from the old connection must not make
 * the newly exported watch appear active.
 */
internal class DevConnectionStateObserver(
    private val scope: CoroutineScope,
    private val emit: (Boolean) -> Unit,
) {
    private val lock = Any()
    private var generation = 0L
    private var source: Any? = null
    private var active = false
    private var collector: Job? = null

    fun update(source: Any?, state: StateFlow<Boolean>?) {
        require((source == null) == (state == null)) { "source and state must change together" }

        val previousCollector: Job?
        val selectedGeneration: Long
        synchronized(lock) {
            if (this.source === source) return
            generation += 1
            selectedGeneration = generation
            this.source = source
            previousCollector = collector
            collector = null
            if (active) {
                active = false
                emit(false)
            }
        }
        previousCollector?.cancel()
        if (source == null || state == null) return

        val nextCollector = scope.launch {
            state.collect { value ->
                synchronized(lock) {
                    if (
                        generation != selectedGeneration ||
                        this@DevConnectionStateObserver.source !== source
                    ) {
                        return@collect
                    }
                    if (active != value) {
                        active = value
                        emit(value)
                    }
                }
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
