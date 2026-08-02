/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import kotlin.test.Test
import kotlin.test.assertEquals

class ProfileSwitchCoordinatorTest {
    @Test
    fun `applies configured profiles only for connection transitions`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val watches = MutableStateFlow(emptyList<ProfileWatchState>())
            val settings = mapOf(
                "AA_BB_CC_DD_EE_FF.profile.connected" to "meeting",
                "AA_BB_CC_DD_EE_FF.profile.disconnected" to "general",
            )
            val applied = mutableListOf<String>()
            val coordinator = ProfileSwitchCoordinator(
                watches,
                settings::getValue,
                ProfileSetter { applied += it },
                scope,
            )

            coordinator.start()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", false))
            yield()
            assertEquals(emptyList(), applied)

            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", true))
            yield()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", false))
            yield()
            assertEquals(listOf("meeting", "general"), applied)
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `a re-added watch does not trigger a profile until its next transition`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val watches = MutableStateFlow(emptyList<ProfileWatchState>())
            val settings = mapOf(
                "AA_BB_CC_DD_EE_FF.profile.connected" to "meeting",
                "AA_BB_CC_DD_EE_FF.profile.disconnected" to "general",
            )
            val applied = mutableListOf<String>()
            val coordinator = ProfileSwitchCoordinator(
                watches,
                { settings[it].orEmpty() },
                ProfileSetter { applied += it },
                scope,
            )

            coordinator.start()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", true))
            yield()
            watches.value = emptyList()
            yield()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", false))
            yield()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", true))
            yield()
            assertEquals(listOf("meeting"), applied)
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `empty profile settings are no-ops`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val watches = MutableStateFlow(emptyList<ProfileWatchState>())
            val applied = mutableListOf<String>()
            val coordinator = ProfileSwitchCoordinator(
                watches,
                { "" },
                ProfileSetter { applied += it },
                scope,
            )

            coordinator.start()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", false))
            yield()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", true))
            yield()
            watches.value = listOf(ProfileWatchState("AA:BB:CC:DD:EE:FF", false))
            yield()
            assertEquals(emptyList(), applied)
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `a slow profile call coalesces later transitions per watch`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val watches = MutableStateFlow(emptyList<ProfileWatchState>())
            val firstStarted = CompletableDeferred<Unit>()
            val releaseFirst = CompletableDeferred<Unit>()
            val applied = mutableListOf<String>()
            val coordinator = ProfileSwitchCoordinator(
                watches,
                { key ->
                    when {
                        key.endsWith(".profile.connected") -> "connected-$key"
                        else -> "disconnected-$key"
                    }
                },
                ProfileSetter { profile ->
                    applied += profile
                    if (applied.size == 1) {
                        firstStarted.complete(Unit)
                        releaseFirst.await()
                    }
                },
                scope,
            )

            coordinator.start()
            val first = ProfileWatchState("AA:BB:CC:DD:EE:FF", false)
            val second = ProfileWatchState("11:22:33:44:55:66", false)
            watches.value = listOf(first, second)
            yield()
            watches.value = listOf(first.copy(connected = true), second)
            firstStarted.await()
            watches.value = listOf(first.copy(connected = true), second.copy(connected = true))
            yield()
            watches.value = listOf(first.copy(connected = true), second)
            yield()

            releaseFirst.complete(Unit)
            withTimeout(1_000) {
                while (applied.size < 2) yield()
            }
            assertEquals(
                listOf(
                    "connected-AA_BB_CC_DD_EE_FF.profile.connected",
                    "disconnected-11_22_33_44_55_66.profile.disconnected",
                ),
                applied,
            )
        } finally {
            scope.cancel()
        }
    }
}
