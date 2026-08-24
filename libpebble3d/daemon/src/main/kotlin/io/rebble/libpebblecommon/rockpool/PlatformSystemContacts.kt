/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import androidx.compose.ui.graphics.ImageBitmap
import io.rebble.libpebblecommon.contacts.SystemContact
import io.rebble.libpebblecommon.contacts.SystemContacts
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import java.util.concurrent.atomic.AtomicLong

/** Read-only Sailfish QtContacts bridge for libpebble3's generic contact store. */
internal class PlatformSystemContacts(
    private val controller: PlatformProviderController,
) : SystemContacts {
    private val changes = MutableSharedFlow<Unit>(extraBufferCapacity = 1)
    private val revision = AtomicLong()

    init {
        controller.addContactChangedListener {
            revision.incrementAndGet()
            changes.tryEmit(Unit)
        }
    }

    override fun registerForContactsChanges(): Flow<Unit> = changes

    override suspend fun getContacts(): List<SystemContact> {
        val startRevision = revision.get()
        val contacts = ArrayList<SystemContact>()
        var offset = 0
        do {
            val result = controller.queryContactPage(
                kind = PlatformProviderController.CONTACT_QUERY_LIST,
                maxRecords = PlatformProviderController.CONTACT_PAGE_MAX,
                offset = offset,
            )
            val snapshot = (result as? PlatformContactQueryResult.Success)?.snapshot
                ?: throw ContactReadException(result)
            if (snapshot.kind != PlatformProviderController.CONTACT_QUERY_LIST ||
                snapshot.nextOffset != 0 && snapshot.nextOffset <= offset) {
                throw ContactReadException(result)
            }
            contacts += snapshot.contacts.map { SystemContact(it.displayName, it.id) }
            offset = snapshot.nextOffset
        } while (offset != 0)
        if (revision.get() != startRevision) {
            throw ContactReadException(null)
        }
        return contacts.distinctBy { it.key }
    }

    suspend fun lookupDisplayName(phoneNumber: String): String? {
        if (phoneNumber.isBlank()) return null
        val result = controller.queryContactPage(
            kind = PlatformProviderController.CONTACT_QUERY_PHONE,
            maxRecords = 1,
            offset = 0,
            query = phoneNumber,
        )
        return (result as? PlatformContactQueryResult.Success)
            ?.snapshot?.contacts?.singleOrNull()?.displayName
    }

    // The isolated helper owns QtContacts access; no runtime user permission is required.
    // Provider availability is reported by query failures and change edges, not as permission.
    override fun hasPermission(): Boolean = true

    override suspend fun getContactImage(lookupKey: String): ImageBitmap? = null

    private class ContactReadException(result: PlatformContactQueryResult?) :
        RuntimeException("platform contact read failed: $result")
}
