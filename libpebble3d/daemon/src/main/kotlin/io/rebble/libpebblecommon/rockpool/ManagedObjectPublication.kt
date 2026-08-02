/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

/**
 * Serializes a dynamic D-Bus object's exported state with the map exposed by
 * ObjectManager.GetManagedObjects.
 *
 * The connection lock is always nested inside the object-map lock, matching
 * RockpoolService's reconnect path. A failed connection mutation detaches that
 * connection but still commits the logical map mutation so the next connection
 * can export the authoritative snapshot.
 */
internal class ManagedObjectPublisher<C : Any>(
    private val objectsLock: Any,
    private val connectionLock: Any,
    private val currentConnection: () -> C?,
    private val setConnection: (C?) -> Unit,
) {
    /**
     * Exports a complete logical-object snapshot before publishing its well-known name and
     * making [connection] available to concurrent mutations or connection-loss handling.
     *
     * Keeping [objectsLock] while [exportSnapshot] runs makes an ObjectManager
     * reader observe either the pre-export state on the old connection or the
     * fully exported state on this one. If exporting or name publication fails, the connection
     * is deliberately left unpublished.
     */
    fun publishConnectionAfterExport(
        connection: C,
        publishPublicName: (C) -> Unit = {},
        exportSnapshot: (C) -> Unit,
    ) {
        synchronized(objectsLock) {
            exportSnapshot(connection)
            // A well-known bus name is itself public visibility. Publish it only while the
            // complete object snapshot is exported and dynamic mutations are still excluded.
            publishPublicName(connection)
            synchronized(connectionLock) {
                setConnection(connection)
            }
        }
    }

    fun <T> mutate(block: ManagedObjectMutation<C>.() -> T): ManagedObjectMutationResult<C, T> =
        synchronized(objectsLock) {
            synchronized(connectionLock) {
                val mutation = ManagedObjectMutation(currentConnection())
                val value = try {
                    mutation.block()
                } finally {
                    mutation.lostConnection?.let { lost ->
                        if (currentConnection() === lost) setConnection(null)
                    }
                }
                ManagedObjectMutationResult(
                    connection = mutation.connection,
                    lostConnection = mutation.lostConnection,
                    failure = mutation.failure,
                    value = value,
                )
            }
        }
}

internal class ManagedObjectMutation<C : Any> internal constructor(
    initialConnection: C?,
) {
    var connection: C? = initialConnection
        private set

    var lostConnection: C? = null
        private set

    var failure: Exception? = null
        private set

    /** Export first, then make the object visible to ObjectManager. */
    fun <T> publish(export: (C) -> Unit, makeVisible: () -> T): T {
        mutateConnection(export)
        return makeVisible()
    }

    /** Stop serving the object first, then hide it from ObjectManager. */
    fun <T> remove(unexport: (C) -> Unit, hide: () -> T): T {
        mutateConnection(unexport)
        return hide()
    }

    private fun mutateConnection(action: (C) -> Unit) {
        val active = connection ?: return
        try {
            action(active)
        } catch (e: Exception) {
            if (failure == null) failure = e
            lostConnection = active
            connection = null
        }
    }
}

internal data class ManagedObjectMutationResult<C : Any, T>(
    /** Connection on which every successful export/unexport in this mutation occurred. */
    val connection: C?,
    val lostConnection: C?,
    val failure: Exception?,
    val value: T,
)
