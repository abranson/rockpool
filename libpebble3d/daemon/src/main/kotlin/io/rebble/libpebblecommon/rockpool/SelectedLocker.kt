/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.*
import io.rebble.libpebblecommon.linux.web.AppstoreListingId
import io.rebble.libpebblecommon.linux.web.RebbleAppstore
import io.rebble.libpebblecommon.ui.RockpoolAppStore
import io.rebble.libpebblecommon.web.LockerModel
import io.rebble.libpebblecommon.web.LockerModelWrapper
import kotlinx.coroutines.CancellationException
import kotlin.uuid.Uuid

/** Locker routing is independent of the Rebble watchapp account and timeline session. */
internal class SelectedLocker(
    private val rebble: WebServices,
    private val stores: RockpoolAppStore,
    private val credentials: CoreStoreCredentials,
    private val rebbleToken: () -> String?,
    private val firestore: CoreCloudLocker = CoreCloudLocker(),
) : WebServices by rebble {
    class Session(
        val selection: RockpoolAppStore.Selection,
        val core: CoreStoreCredentials.Session?,
        val rebble: WebAccountSession?,
        val bearer: String?,
    ) : StoreLockerSession {
        override val coreLocker: Boolean get() = core != null
    }

    override fun currentLockerSession(): Session? {
        val store = stores.selection.value
        if (store.source.core) return credentials.currentSession()?.let { Session(store, it, null, null) }
        val token = rebbleToken() ?: return null
        val session = rebble.currentAccountSession() ?: return null
        return Session(store, null, session, token).takeIf { isCurrentAccountSession(it) }
    }

    override fun isCurrentAccountSession(session: WebAccountSession): Boolean {
        if (session !is Session) return rebble.isCurrentAccountSession(session)
        return stores.isCurrent(session.selection) && when {
            session.core != null -> credentials.withCurrentSession(session.core) { true } == true
            session.rebble != null -> session.bearer == rebbleToken() && rebble.isCurrentAccountSession(session.rebble)
            else -> false
        }
    }

    override suspend fun fetchLocker(): LockerModelWrapper? = currentLockerSession()?.let { fetchLocker(it) }

    override suspend fun fetchLocker(session: WebAccountSession): LockerModelWrapper? {
        if (session !is Session || !isCurrentAccountSession(session)) return null
        val result = if (session.core != null) {
            val token = credentials.token(session.core) ?: return null
            val docs = firestore.entries(session.core.uid, token)
            val unresolved = mutableSetOf<Uuid>()
            val entries = docs.groupBy { it.build.uuid }.mapNotNull { (uuid, group) ->
                if (!isCurrentAccountSession(session)) return null
                val build = group.singleOrNull()?.build
                val entry = try {
                    build?.let { firestore.resolve(it) }
                } catch (e: CancellationException) {
                    throw e
                } catch (_: Exception) {
                    null
                }
                if (entry == null) unresolved += uuid
                entry
            }
            LockerModelWrapper(LockerModel(entries), unresolved)
        } else rebble.fetchLocker(session.rebble!!) ?: return null
        return result.copy(preserveInstalledBuilds = true).takeIf { isCurrentAccountSession(session) }
    }

    /** UUIDs of every entry, including sources Rockpool does not recognise. */
    suspend fun members(session: Session): Set<Uuid> {
        check(isCurrentAccountSession(session))
        val ids = if (session.core != null) {
            val token = checkNotNull(credentials.token(session.core))
            firestore.entries(session.core.uid, token).map { it.build.uuid }.toSet()
        } else {
            val snapshot = checkNotNull(rebble.fetchLocker(session.rebble!!))
            check(snapshot.locker.nextPageURL.isNullOrEmpty())
            snapshot.locker.applications.map { Uuid.parse(it.uuid) }.toSet() + snapshot.failedToFetchUuids
        }
        check(isCurrentAccountSession(session))
        return ids
    }

    suspend fun add(session: Session, uuid: Uuid, listing: AppstoreListingId): Boolean {
        if (!isCurrentAccountSession(session)) return false
        return if (session.core != null) {
            val token = credentials.token(session.core) ?: return false
            firestore.add(session.core.uid, token, uuid, listing) { isCurrentAccountSession(session) }
        } else !listing.core && RebbleAppstore.addToLocker(uuid.toString(), session.bearer)
    }

    suspend fun replace(session: Session, uuid: Uuid, from: AppstoreListingId, to: AppstoreListingId): Boolean {
        if (!isCurrentAccountSession(session)) return false
        val core = session.core ?: return add(session, uuid, to)
        val token = credentials.token(core) ?: return false
        return firestore.replace(core.uid, token, uuid, from, to) { isCurrentAccountSession(session) }
    }

    override suspend fun removeFromLocker(id: Uuid): Boolean =
        currentLockerSession()?.let { removeFromLocker(id, it) } ?: false

    override suspend fun removeFromLocker(id: Uuid, session: WebAccountSession): Boolean {
        if (session !is Session || !isCurrentAccountSession(session)) return false
        if (session.core != null) {
            val token = credentials.token(session.core) ?: return false
            return firestore.remove(session.core.uid, token, id) { isCurrentAccountSession(session) }
        }
        return id !in members(session) || isCurrentAccountSession(session) && rebble.removeFromLocker(id, session.rebble!!)
    }
}
