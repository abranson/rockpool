/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.database.dao.AppWithCount
import io.rebble.libpebblecommon.database.entity.MuteState
import io.rebble.libpebblecommon.rockpool.PlatformNotificationFilter
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

internal data class RockpoolNotificationSource(
    val sourceId: String,
    val name: String,
    val icon: String,
    val enabled: Int,
    val colorName: String?,
    val iconCode: String?,
)

/**
 * Convert libpebble3's account-global notification applications into the record carried by the
 * existing org.rockpool NotificationFilterChanged signal.
 */
internal fun rockpoolNotificationSources(
    apps: List<AppWithCount>,
    configured: Map<String, PlatformNotificationFilter>,
): List<RockpoolNotificationSource> = apps.map { appWithCount ->
    val app = appWithCount.app
    RockpoolNotificationSource(
        sourceId = app.packageName,
        name = app.name,
        icon = "",
        enabled = if (app.muteState == MuteState.Always) {
            0
        } else {
            configured[app.packageName]?.mode ?: 2
        },
        colorName = app.colorName,
        iconCode = app.iconCode,
    )
}

/**
 * Stateful diff for a service-wide notification-app observer. The initial snapshot is returned so
 * already-exported compatibility objects are populated even if their C++ construction races the
 * first Room emission. Removed sources use the historical enabled=-1 signal convention.
 */
internal class RockpoolNotificationSourceTracker {
    private var previous = emptyMap<String, RockpoolNotificationSource>()

    @Synchronized
    fun update(current: List<RockpoolNotificationSource>): List<RockpoolNotificationSource> {
        val next = current.associateBy { it.sourceId }
        val changed = current.filter { previous[it.sourceId] != it }
        val removed = (previous.keys - next.keys).sorted().map { sourceId ->
            RockpoolNotificationSource(sourceId, "", "", -1, null, null)
        }
        previous = next
        return changed + removed
    }
}

/**
 * One ordered owner for complete source snapshots and their compatibility signals. Inputs only
 * invalidate; the worker re-reads the latest Room/config state before calculating each diff.
 */
internal class RockpoolNotificationSourcePublisher(
    private val snapshot: () -> List<RockpoolNotificationSource>,
    private val emit: (RockpoolNotificationSource) -> Unit,
    private val onFailure: (Throwable) -> Unit = {},
) {
    private val invalidations = Channel<Unit>(Channel.CONFLATED)
    private val tracker = RockpoolNotificationSourceTracker()

    fun start(scope: CoroutineScope) {
        scope.launch {
            for (ignored in invalidations) {
                try {
                    tracker.update(snapshot()).forEach(emit)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    onFailure(e)
                }
            }
        }
    }

    fun invalidate() {
        invalidations.trySend(Unit)
    }
}
