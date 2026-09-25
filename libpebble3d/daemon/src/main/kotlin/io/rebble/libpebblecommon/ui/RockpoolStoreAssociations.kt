package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.disk.pbw.StorePbwMetadata
import io.rebble.libpebblecommon.linux.web.RebbleAppstore
import io.rebble.libpebblecommon.locker.LockerWrapper
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.first
import kotlin.uuid.Uuid

/** One background lookup per unlinked UUID per daemon run; restart retries failed lookups. */
internal class RockpoolStoreAssociations(
    private val libPebble: LibPebble,
    private val changed: () -> Unit,
    private val lookup: suspend (Uuid) -> StorePbwMetadata? = RebbleAppstore::findStoreMetadata,
) {
    private val attempted = mutableSetOf<Uuid>()
    private val logger = Logger.withTag("RockpoolStoreAssociations")

    suspend fun refresh(uuids: List<Uuid>) {
        attempted.retainAll(uuids.toSet())
        for (uuid in uuids) {
            if (uuid in attempted) continue
            try {
                val app = libPebble.getLockerApp(uuid).first() as? LockerWrapper.NormalApp ?: continue
                if (!app.sideloaded || !app.properties.storeId.isNullOrBlank()) continue
                val metadata = lookup(uuid)
                if (metadata != null && metadata.uuid == uuid && libPebble.associateStoreMetadata(metadata)) {
                    changed()
                }
                attempted.add(uuid)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                attempted.add(uuid)
                logger.w(e) { "Could not associate sideloaded app $uuid with its store entry" }
            }
        }
    }
}
