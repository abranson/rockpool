/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.database.asMillisecond
import io.rebble.libpebblecommon.database.dao.AppWithCount
import io.rebble.libpebblecommon.database.entity.MuteState
import io.rebble.libpebblecommon.database.entity.NotificationAppItem
import io.rebble.libpebblecommon.rockpool.PlatformNotificationFilter
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.time.Instant

class RockpoolNotificationSourcesTest {
    @Test
    fun `mapping preserves conditional mode and hard mute`() {
        val conditional = app("conditional", "Conditional", MuteState.Never)
        val muted = app("muted", "Muted", MuteState.Always)

        assertEquals(
            listOf(
                RockpoolNotificationSource("conditional", "Conditional", "", 1, null, null),
                RockpoolNotificationSource("muted", "Muted", "", 0, null, null),
            ),
            rockpoolNotificationSources(
                listOf(AppWithCount(conditional, 1), AppWithCount(muted, 1)),
                mapOf(
                    "conditional" to PlatformNotificationFilter.fromMode(1, "Conditional", ""),
                    "muted" to PlatformNotificationFilter.fromMode(2, "Muted", ""),
                ),
            ),
        )
    }

    @Test
    fun `tracker emits additions changes and one removal`() {
        val tracker = RockpoolNotificationSourceTracker()
        val first = RockpoolNotificationSource("one", "One", "", 2, null, null)
        val second = RockpoolNotificationSource("two", "Two", "", 1, null, null)

        assertEquals(listOf(first), tracker.update(listOf(first)))
        assertEquals(emptyList(), tracker.update(listOf(first)))
        assertEquals(listOf(second), tracker.update(listOf(first, second)))

        val changed = first.copy(enabled = 0)
        assertEquals(listOf(changed), tracker.update(listOf(changed, second)))

        val appearance = changed.copy(colorName = "Red")
        assertEquals(listOf(appearance), tracker.update(listOf(appearance, second)))
        assertEquals(
            listOf(RockpoolNotificationSource("two", "", "", -1, null, null)),
            tracker.update(listOf(appearance)),
        )
        assertEquals(emptyList(), tracker.update(listOf(appearance)))
    }

    @Test
    fun `publisher emits old then newest snapshot when its first emission is held`() = runBlocking {
        val old = RockpoolNotificationSource("mail", "Mail", "", 2, null, null)
        val newest = old.copy(enabled = 0)
        var snapshot = listOf(old)
        val emitted = mutableListOf<RockpoolNotificationSource>()
        val firstEmissionEntered = CompletableDeferred<Unit>()
        val releaseFirstEmission = CountDownLatch(1)
        val newestEmission = CompletableDeferred<Unit>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val publisher = RockpoolNotificationSourcePublisher(
            snapshot = { snapshot },
            emit = { source ->
                synchronized(emitted) { emitted += source }
                if (source == old) {
                    firstEmissionEntered.complete(Unit)
                    check(releaseFirstEmission.await(5, TimeUnit.SECONDS))
                }
                if (source == newest) newestEmission.complete(Unit)
            },
        )
        publisher.start(scope)
        try {
            publisher.invalidate()
            firstEmissionEntered.await()

            snapshot = listOf(newest)
            publisher.invalidate()
            releaseFirstEmission.countDown()

            withTimeout(5_000) { newestEmission.await() }
            assertEquals(listOf(old, newest), synchronized(emitted) { emitted.toList() })
        } finally {
            releaseFirstEmission.countDown()
            scope.cancel()
        }
    }

    private fun app(packageName: String, name: String, muteState: MuteState): NotificationAppItem {
        val epoch = Instant.fromEpochSeconds(0).asMillisecond()
        return NotificationAppItem(
            packageName = packageName,
            name = name,
            muteState = muteState,
            channelGroups = emptyList(),
            stateUpdated = epoch,
            lastNotified = epoch,
            muteExpiration = null,
            vibePatternName = null,
            colorName = null,
            iconCode = null,
        )
    }
}
