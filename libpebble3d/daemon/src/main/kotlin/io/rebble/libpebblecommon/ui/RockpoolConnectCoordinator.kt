/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch

/**
 * Gives the newest legacy ConnectWatch request exclusive ownership of transport rediscovery.
 * libpebble3's BLE and Classic scanners share one adapter and deliberately stop each other, so
 * overlapping rediscovery jobs must not be allowed to cancel one another nondeterministically.
 */
internal class RockpoolConnectCoordinator(
    private val scope: CoroutineScope,
    private val connect: suspend (String) -> Unit,
) {
    private val lock = Any()
    private var generation = 0L
    private var job: Job? = null

    fun start(address: String) {
        val owner: Long
        val previous: Job?
        val next: Job
        synchronized(lock) {
            owner = ++generation
            previous = job
            next = scope.launch(start = CoroutineStart.LAZY) {
                try {
                    previous?.cancelAndJoin()
                    if (synchronized(lock) { owner == generation }) {
                        connect(address)
                    }
                } finally {
                    synchronized(lock) {
                        if (owner == generation) job = null
                    }
                }
            }
            job = next
        }
        previous?.cancel()
        next.start()
    }
}
