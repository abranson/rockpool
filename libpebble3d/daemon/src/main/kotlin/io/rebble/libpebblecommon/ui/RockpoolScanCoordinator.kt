/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * Serializes the ownership of a compatibility scan request without making StopScan wait for a
 * blocking BlueZ refresh. A refresh may finish after a newer StartScan or StopScan; in that case
 * it owns neither the bonded-address state nor the scanner.
 */
internal class RockpoolScanCoordinator(
    private val scope: CoroutineScope,
    private val reconcileBondedWatches: suspend (beginCommit: () -> Boolean) -> Unit,
    private val refreshBondedAddresses: suspend () -> Set<String>,
    private val applyBondedAddresses: (Set<String>) -> Unit,
    private val recomputeResults: () -> Unit,
    private val startScan: () -> Unit,
    private val stopScan: () -> Unit,
    private val onReconcileFailure: (Throwable) -> Unit,
    private val onRefreshFailure: (Throwable) -> Unit,
    private val onScanFailure: (String, Throwable) -> Unit,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    private val lock = Any()
    private var generation = 0L

    fun start() {
        val owner = synchronized(lock) { ++generation }
        scope.launch(dispatcher) {
            try {
                reconcileBondedWatches { owns(owner) }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                onReconcileFailure(e)
            }
            if (!owns(owner)) return@launch

            val bonded = try {
                refreshBondedAddresses()
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                onRefreshFailure(e)
                emptySet()
            }
            synchronized(lock) {
                if (owner != generation) return@launch
                applyBondedAddresses(bonded)
                // The watch flow need not emit again after its filter changes.
                recomputeResults()
                runScanOperation("StartScan", startScan)
            }
        }
    }

    fun stop() {
        synchronized(lock) { ++generation }
        // stopScan only cancels scanner jobs. Run it before returning so a pairing rediscovery
        // cannot be stopped later by a queued compatibility StopScan callback.
        runScanOperation("StopScan", stopScan)
    }

    private fun owns(owner: Long): Boolean = synchronized(lock) { owner == generation }

    private fun runScanOperation(operation: String, block: () -> Unit) {
        try {
            block()
        } catch (e: CancellationException) {
            throw e
        } catch (e: Throwable) {
            onScanFailure(operation, e)
        }
    }
}
