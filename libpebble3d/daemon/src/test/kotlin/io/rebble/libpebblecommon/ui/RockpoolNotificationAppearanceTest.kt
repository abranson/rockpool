/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.database.asMillisecond
import io.rebble.libpebblecommon.database.dao.AppWithCount
import io.rebble.libpebblecommon.database.entity.MuteState
import io.rebble.libpebblecommon.database.entity.NotificationAppItem
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import java.util.concurrent.CountDownLatch
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.time.Instant

class RockpoolNotificationAppearanceTest {
    @Test
    fun `rapid color then icon preserves both fields across delayed Room publication`() = runBlocking {
        val initial = app()
        val state = MutableStateFlow(listOf(AppWithCount(initial, 1)))
        val writes = mutableListOf<RockpoolNotificationAppearance>()
        val firstWrite = CompletableDeferred<Unit>()
        val releaseFirst = CountDownLatch(1)
        val base = FakeLibPebble()
        val libPebble = object : LibPebble by base {
            override fun notificationApps(): Flow<List<AppWithCount>> = state

            override fun updateNotificationAppState(
                packageName: String,
                vibePatternName: String?,
                colorName: String?,
                iconCode: String?,
            ) {
                val appearance = RockpoolNotificationAppearance(
                    vibePatternName,
                    colorName,
                    iconCode,
                )
                synchronized(writes) { writes += appearance }
                val publish = {
                    state.value = listOf(
                        AppWithCount(
                            state.value.single().app.copy(
                                vibePatternName = vibePatternName,
                                colorName = colorName,
                                iconCode = iconCode,
                            ),
                            1,
                        ),
                    )
                }
                if (synchronized(writes) { writes.size } == 1) {
                    firstWrite.complete(Unit)
                    thread(isDaemon = true, name = "appearance-test-publish") {
                        releaseFirst.await()
                        publish()
                    }
                } else {
                    publish()
                }
            }
        }
        val coordinator = RockpoolNotificationAppearanceCoordinator(libPebble)

        val color = async { coordinator.setColor(PACKAGE, "Red") }
        firstWrite.await()
        val icon = async { coordinator.setIcon(PACKAGE, "system://images/TIMELINE_ALARM_CLOCK") }
        yield()
        releaseFirst.countDown()

        assertTrue(color.await())
        assertTrue(icon.await())
        assertEquals(
            listOf(
                RockpoolNotificationAppearance(null, "Red", null),
                RockpoolNotificationAppearance(
                    null,
                    "Red",
                    "system://images/TIMELINE_ALARM_CLOCK",
                ),
            ),
            synchronized(writes) { writes.toList() },
        )
    }

    private fun app(): NotificationAppItem {
        val epoch = Instant.fromEpochSeconds(0).asMillisecond()
        return NotificationAppItem(
            packageName = PACKAGE,
            name = "Messages",
            muteState = MuteState.Never,
            channelGroups = emptyList(),
            stateUpdated = epoch,
            lastNotified = epoch,
            muteExpiration = null,
            vibePatternName = null,
            colorName = null,
            iconCode = null,
        )
    }

    private companion object {
        const val PACKAGE = "x-nemo.messaging.sms"
    }
}
