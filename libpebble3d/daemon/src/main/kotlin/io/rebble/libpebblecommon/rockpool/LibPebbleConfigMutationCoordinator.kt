/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.LibPebbleConfig
import io.rebble.libpebblecommon.connection.LibPebble
import java.util.IdentityHashMap

internal data class LibPebbleConfigUpdate(
    val previous: LibPebbleConfig,
    val current: LibPebbleConfig,
)

/** Serializes read-modify-write updates to one libpebble configuration record. */
internal class LibPebbleConfigMutationCoordinator(
    private val current: () -> LibPebbleConfig,
    private val update: (LibPebbleConfig) -> Unit,
) {
    private val lock = Any()
    private val listenerLock = Any()
    private val listeners = mutableListOf<(LibPebbleConfigUpdate) -> Unit>()

    fun addListener(listener: (LibPebbleConfigUpdate) -> Unit) {
        synchronized(listenerLock) { listeners += listener }
    }

    fun mutate(transform: (LibPebbleConfig) -> LibPebbleConfig) {
        val change = synchronized(lock) {
            val previous = current()
            val next = transform(previous)
            if (next == previous) return@synchronized null
            update(next)
            LibPebbleConfigUpdate(previous, next)
        } ?: return
        synchronized(listenerLock) { listeners.toList() }.forEach { it(change) }
    }

    companion object {
        private val coordinators = IdentityHashMap<LibPebble, LibPebbleConfigMutationCoordinator>()

        fun forLibPebble(libPebble: LibPebble): LibPebbleConfigMutationCoordinator =
            synchronized(coordinators) {
                coordinators.getOrPut(libPebble) {
                    LibPebbleConfigMutationCoordinator(
                        current = { libPebble.config.value },
                        update = libPebble::updateConfig,
                    )
                }
            }
    }
}
