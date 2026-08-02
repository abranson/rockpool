/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.seconds

/**
 * Reproduces rockworkd's one-button, mixed-transport discovery on top of libpebble3's deliberately
 * exclusive BLE and BR/EDR scanners. Results are accumulated by [RockworkService] while this
 * session runs; each scanner owns the adapter for one half of the legacy discovery window.
 */
internal class RockworkMixedScanSession(
    private val scope: CoroutineScope,
    private val startBle: () -> Unit,
    private val stopBle: () -> Unit,
    private val startClassic: () -> Unit,
    private val stopClassic: () -> Unit,
    private val classicAvailable: Boolean,
    private val onStarted: () -> Unit,
    private val onActiveChanged: (Boolean) -> Unit,
    private val onFailure: (String, Throwable) -> Unit,
    private val waitForPhase: suspend () -> Unit = { delay(SCAN_PHASE_DURATION) },
) {
    private val lock = Any()
    private var generation = 0L
    private var job: Job? = null
    private var active = false

    fun start() {
        val owner: Long
        val next: Job
        synchronized(lock) {
            owner = ++generation
            job?.cancel()
            active = true
            onStarted()
            onActiveChanged(true)
            next = scope.launch(start = CoroutineStart.LAZY) {
                try {
                    if (!runIfCurrent(owner) {
                            stopBoth()
                            runOperation("StartScan/BLE", startBle)
                        }) return@launch
                    waitForPhase()

                    if (!runIfCurrent(owner) {
                            if (classicAvailable) {
                                runOperation("StopScan/BLE", stopBle)
                                runOperation("StartScan/Classic", startClassic)
                            }
                        }) return@launch
                    waitForPhase()
                } catch (e: CancellationException) {
                    throw e
                } finally {
                    finish(owner)
                }
            }
            job = next
        }
        next.start()
    }

    fun stop() {
        val previous: Job?
        val notify: Boolean
        synchronized(lock) {
            ++generation
            previous = job
            job = null
            notify = active
            active = false
            stopBoth()
        }
        previous?.cancel()
        if (notify) onActiveChanged(false)
    }

    fun isActive(): Boolean = synchronized(lock) { active }

    private fun finish(owner: Long) {
        val notify = synchronized(lock) {
            if (owner != generation) return
            job = null
            val wasActive = active
            active = false
            stopBoth()
            wasActive
        }
        if (notify) onActiveChanged(false)
    }

    private fun runIfCurrent(owner: Long, operation: () -> Unit): Boolean = synchronized(lock) {
        if (owner != generation || !active) return@synchronized false
        operation()
        true
    }

    private fun stopBoth() {
        runOperation("StopScan/BLE", stopBle)
        runOperation("StopScan/Classic", stopClassic)
    }

    private fun runOperation(name: String, operation: () -> Unit) {
        try {
            operation()
        } catch (e: CancellationException) {
            throw e
        } catch (e: Throwable) {
            onFailure(name, e)
        }
    }

    private companion object {
        val SCAN_PHASE_DURATION = 15.seconds
    }
}
