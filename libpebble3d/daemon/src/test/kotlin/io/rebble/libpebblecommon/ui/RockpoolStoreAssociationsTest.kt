package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.database.entity.LockerEntryAppstoreData
import io.rebble.libpebblecommon.disk.pbw.StorePbwMetadata
import io.rebble.libpebblecommon.locker.AppProperties
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.uuid.Uuid

class RockpoolStoreAssociationsTest {
    @Test
    fun linksExistingAndNewSideloadsOnceAndPublishesMetadataChanges() = runBlocking {
        val sideload = app()
        val linked = app(storeId = "already-linked")
        val remote = app(sideloaded = false)
        val missing = app()
        val apps = mutableMapOf(*listOf(sideload, linked, remote, missing)
            .map { it.properties.id to it }.toTypedArray())
        val lookups = mutableListOf<Uuid>()
        val commits = mutableListOf<Uuid>()
        var changes = 0
        val libPebble = object : LibPebble by FakeLibPebble() {
            override fun getLockerApp(id: Uuid) = flowOf(apps[id])
            override suspend fun associateStoreMetadata(storeMetadata: StorePbwMetadata): Boolean {
                commits.add(storeMetadata.uuid)
                return true
            }
        }
        val worker = RockpoolStoreAssociations(libPebble, changed = { changes++ }, lookup = {
            lookups.add(it)
            if (it == missing.properties.id) null else metadata(it)
        })
        worker.refresh(apps.keys.toList())
        worker.refresh(apps.keys.toList())
        assertEquals(listOf(sideload.properties.id, missing.properties.id), lookups)
        assertEquals(listOf(sideload.properties.id), commits)
        assertEquals(1, changes)
        val added = app()
        apps[added.properties.id] = added
        worker.refresh(apps.keys.toList())
        assertEquals(added.properties.id, commits.last())
        assertEquals(2, changes)
    }

    @Test
    fun mismatchedResultsDoNotCommitAndCancellationDoesNotConsumeRetry() = runBlocking {
        val app = app()
        var commits = 0
        val libPebble = object : LibPebble by FakeLibPebble() {
            override fun getLockerApp(id: Uuid) = flowOf(app)
            override suspend fun associateStoreMetadata(storeMetadata: StorePbwMetadata): Boolean {
                commits++
                return true
            }
        }
        val ids = listOf(app.properties.id)
        val mismatched = RockpoolStoreAssociations(libPebble, changed = {}, lookup = { metadata(Uuid.random()) })
        mismatched.refresh(ids)
        assertEquals(0, commits)
        var calls = 0
        val cancelled = RockpoolStoreAssociations(libPebble, changed = {}, lookup = {
            if (calls++ == 0) throw CancellationException()
            metadata(it)
        })
        assertFailsWith<CancellationException> { cancelled.refresh(ids) }
        cancelled.refresh(ids)
        assertEquals(1, commits)
    }

    private fun app(storeId: String? = null, sideloaded: Boolean = true) = LockerWrapper.NormalApp(
        AppProperties(Uuid.random(), AppType.Watchface, "Local face", "Author", null,
            emptyList(), "1.0", null, null, null, null, 0, null, storeId, emptyList()),
        sideloaded = sideloaded, configurable = true, sync = true,
    )

    private fun metadata(uuid: Uuid) = StorePbwMetadata(
        uuid, LockerEntryAppstoreData(0, "author", false, "", "", "", null, storeId = "store-id"),
        emptyList(),
    )
}
