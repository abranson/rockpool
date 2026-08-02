/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.LibPebble
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.time.Duration.Companion.seconds

internal data class RockworkNotificationAppearance(
    val vibePatternName: String?,
    val colorName: String?,
    val iconCode: String?,
)

/**
 * Serializes the legacy UI's separate color/icon setters over libpebble3's combined asynchronous
 * update. A short-lived desired-state barrier preserves the first field while Room is still
 * publishing it and the second setter arrives.
 */
internal class RockworkNotificationAppearanceCoordinator(
    private val libPebble: LibPebble,
) {
    private val logger = Logger.withTag("RockworkNotificationAppearance")
    private val mutex = Mutex()
    private val pending = mutableMapOf<String, Pending>()

    private data class Pending(
        val expected: RockworkNotificationAppearance,
        val preceding: Set<RockworkNotificationAppearance>,
    )

    suspend fun setColor(sourceId: String, colorName: String): Boolean = update(sourceId) {
        it.copy(colorName = colorName.ifEmpty { null })
    }

    suspend fun setIcon(sourceId: String, iconCode: String): Boolean = update(sourceId) {
        it.copy(iconCode = iconCode.ifEmpty { null })
    }

    private suspend fun update(
        sourceId: String,
        transform: (RockworkNotificationAppearance) -> RockworkNotificationAppearance,
    ): Boolean = mutex.withLock {
        if (sourceId.isBlank()) return@withLock false
        val actual = current(sourceId) ?: run {
            pending.remove(sourceId)
            return@withLock false
        }
        val previousPending = pending[sourceId]
        val current = resolve(sourceId, actual)
        val updated = transform(current)
        if (updated == current) return@withLock true

        pending[sourceId] = Pending(
            expected = updated,
            preceding = buildSet {
                add(actual)
                previousPending?.let {
                    add(it.expected)
                    addAll(it.preceding)
                }
            },
        )
        libPebble.updateNotificationAppState(
            packageName = sourceId,
            vibePatternName = updated.vibePatternName,
            colorName = updated.colorName,
            iconCode = updated.iconCode,
        )
        val applied = withTimeoutOrNull(APPLY_TIMEOUT) {
            libPebble.notificationApps().first { apps ->
                apps.firstOrNull { it.app.packageName == sourceId }
                    ?.app?.appearance() == updated
            }
        }
        if (applied != null && pending[sourceId]?.expected == updated) {
            pending.remove(sourceId)
        } else if (applied == null) {
            logger.w { "notification appearance persistence is delayed for $sourceId" }
        }
        true
    }

    private suspend fun current(sourceId: String): RockworkNotificationAppearance? =
        libPebble.notificationApps().first()
            .firstOrNull { it.app.packageName == sourceId }
            ?.app?.appearance()

    private fun resolve(
        sourceId: String,
        actual: RockworkNotificationAppearance,
    ): RockworkNotificationAppearance {
        val desired = pending[sourceId] ?: return actual
        return when {
            actual == desired.expected -> {
                pending.remove(sourceId)
                actual
            }
            actual in desired.preceding -> desired.expected
            else -> {
                // A newer writer outside this compatibility setter won.
                pending.remove(sourceId)
                actual
            }
        }
    }

    private companion object {
        val APPLY_TIMEOUT = 5.seconds
    }
}

private fun io.rebble.libpebblecommon.database.entity.NotificationAppItem.appearance() =
    RockworkNotificationAppearance(vibePatternName, colorName, iconCode)
