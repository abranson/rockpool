/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.linux.web.AppstoreListingId
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.rockpool.REBBLE_FEED
import io.rebble.libpebblecommon.rockpool.SelectedLocker
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.serialization.json.*
import kotlin.uuid.Uuid

internal class LockerCopy(private val libPebble: LibPebble, private val locker: SelectedLocker) {
    private class Preview(val key: String, val session: SelectedLocker.Session, val apps: Map<Uuid, String>)
    private val mutex = Mutex()
    private var pending: Preview? = null

    private suspend fun installed(): Map<Uuid, String> {
        val hardware = libPebble.watches.value.filterIsInstance<KnownPebbleDevice>().singleOrNull()?.watchType?.watchType
            ?: error("A single known watch is required")
        val entries = listOf(AppType.Watchface, AppType.Watchapp).flatMap {
            libPebble.getLocker(it, null, Int.MAX_VALUE).first()
        }
        return rockpoolInstalledApplications(entries, hardware).filterIsInstance<LockerWrapper.NormalApp>().mapNotNull { app ->
            val id = AppstoreListingId.parse(app.properties.storeId.orEmpty()) ?: return@mapNotNull null
            // Legacy sideload associations were inferred from a matching UUID. Only copy a
            // positively identified PBW, or an app received directly from a store locker.
            if (app.sideloaded && !id.core && app.properties.sourceLink != REBBLE_FEED) {
                return@mapNotNull null
            }
            app.properties.id to id.value
        }.toMap()
    }

    suspend fun preview(source: String): String = mutex.withLock {
        pending = null
        val session = checkNotNull(locker.currentLockerSession()).also { check(it.selection.source.value == source) }
        val present = locker.members(session)
        val missing = installed().filter { (uuid, id) ->
            uuid !in present && (session.selection.source.core || AppstoreListingId.parse(id)?.core == false)
        }
        check(locker.isCurrentAccountSession(session))
        val key = Uuid.random().toString()
        pending = Preview(key, session, missing)
        buildJsonObject { put("key", key); put("count", missing.size) }.toString()
    }

    suspend fun copy(key: String): Int = mutex.withLock {
        val plan = checkNotNull(pending?.takeIf { it.key == key })
        val (uuid, listing) = plan.apps.entries.first()
        check(locker.isCurrentAccountSession(plan.session))
        if (installed()[uuid] == listing && uuid !in locker.members(plan.session)) {
            check(locker.add(plan.session, uuid, checkNotNull(AppstoreListingId.parse(listing))))
        }
        val remaining = plan.apps - uuid
        pending = if (remaining.isEmpty()) null else Preview(key, plan.session, remaining)
        remaining.size
    }
}
