/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.database.entity.MuteState
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class NotificationFilterCoordinatorTest {
    @Test
    fun `forget persists an empty canonical policy before retiring app and clears migration fallbacks`() =
        runBlocking {
            val source = "org.example.mail"
            val store = FakeStore(
                encodeNotificationFilters(
                    GLOBAL_NOTIFICATION_FILTER_PREFIX,
                    mapOf(source to filter(enabled = false, name = "Mail")),
                ) + mapOf(
                    "watch.one.notifications.old.source" to source,
                    "watch.one.notifications.old.enabled" to "0",
                    "watch.two.notifications.other.source" to "org.example.other",
                    "watch.two.notifications.other.enabled" to "1",
                    "watch.two.health.enabled" to "true",
                )
            )
            val calls = mutableListOf<String>()
            var runtime = emptyMap<String, PlatformNotificationFilter>()
            var listenerCalls = 0
            val coordinator = coordinator(
                store = store,
                runtime = { filters ->
                    calls += "provider"
                    runtime = filters
                    true
                },
                mute = { id, state -> calls += "mute:$id:$state" },
                forget = { id ->
                    calls += "forget:$id"
                    assertFalse(source in runtime)
                },
            )
            coordinator.addListener {
                calls += "listener"
                listenerCalls++
            }

            val result = coordinator.forgetCompatibilityFilter(source)

            assertNotNull(result)
            assertTrue(result.activePinsUpdated)
            assertEquals("provider", calls.first())
            assertTrue(calls.indexOf("provider") < calls.indexOf("forget:$source"))
            assertEquals("listener", calls.last())
            assertEquals(1, listenerCalls)
            assertEquals(
                mapOf(GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING to "true"),
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX),
            )
            assertTrue(store.entries("watch.one.notifications.").isEmpty())
            assertTrue(store.entries("watch.two.notifications.").isEmpty())
            assertEquals("true", store.values["watch.two.health.enabled"])
        }

    @Test
    fun `explicit empty canonical policy survives startup and suppresses watch fallback`() =
        runBlocking {
            val legacySource = "org.example.legacy"
            val store = FakeStore(
                mapOf(
                    GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING to "true",
                    "watch.old.notifications.legacy.source" to legacySource,
                    "watch.old.notifications.legacy.enabled" to "0",
                )
            )
            var runtime: Map<String, PlatformNotificationFilter>? = null
            var muteStates: Map<String, MuteState>? = null
            val coordinator = coordinator(
                store = store,
                runtime = { filters -> runtime = filters; true },
                reconcileMuteStates = { states -> muteStates = states },
            )

            assertTrue(coordinator.reconcilePersistedState())

            assertEquals(emptyMap(), runtime)
            assertEquals(emptyMap(), muteStates)
            assertEquals(
                mapOf(GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING to "true"),
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX),
            )
            assertEquals(0, store.writeCount)
        }

    @Test
    fun `compatibility toggle persists and applies provider policy before legacy signal point`() =
        runBlocking {
            val source = "org.example.chat"
            val store = FakeStore(
                encodeNotificationFilters(
                    GLOBAL_NOTIFICATION_FILTER_PREFIX,
                    mapOf(source to filter(enabled = false, name = "Chat", icon = "chat")),
                )
            )
            val calls = mutableListOf<String>()
            var applied: Map<String, PlatformNotificationFilter>? = null
            val coordinator = coordinator(
                store = store,
                runtime = {
                    calls += "provider"
                    applied = it
                    true
                },
                mute = { id, state -> calls += "mute:$id:$state" },
            )
            coordinator.addListener { calls += "listener" }

            val result = coordinator.setCompatibilityFilter(source, 2)

            assertEquals(filter(true, "Chat", "chat"), result?.filter)
            assertEquals(true, applied?.get(source)?.enabled)
            assertTrue(
                notificationFiltersFromEntries(
                    store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
                ).getValue(source).enabled
            )
            assertEquals(
                listOf("provider", "mute:$source:${MuteState.Never}", "listener"),
                calls,
            )
        }

    @Test
    fun `first compatibility toggle retains the learned application name`() = runBlocking {
        val source = "org.example.chat"
        val store = FakeStore()
        var applied = emptyMap<String, PlatformNotificationFilter>()
        val coordinator = coordinator(
            store = store,
            runtime = { filters -> applied = filters; true },
        )

        val result = coordinator.setCompatibilityFilter(source, 0, "Chat")

        assertEquals(filter(false, "Chat"), result?.filter)
        assertEquals(filter(false, "Chat"), applied[source])
        assertEquals(
            filter(false, "Chat"),
            notificationFiltersFromEntries(
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
            )[source],
        )
    }

    @Test
    fun `conditional compatibility mode survives persistence and remains runtime unmuted`() =
        runBlocking {
            val source = "org.example.chat"
            val store = FakeStore()
            val muteStates = mutableListOf<Pair<String, MuteState>>()
            var applied = emptyMap<String, PlatformNotificationFilter>()
            val coordinator = coordinator(
                store = store,
                runtime = { filters -> applied = filters; true },
                mute = { id, state -> muteStates += id to state },
            )

            val result = coordinator.setCompatibilityFilter(source, 1, "Chat")

            assertEquals(1, result?.filter?.mode)
            assertEquals(1, applied.getValue(source).mode)
            assertEquals(
                "1",
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
                    .entries.single { it.key.endsWith(".enabled") }.value,
            )
            assertEquals(listOf(source to MuteState.Never), muteStates)
            assertEquals(
                1,
                notificationFiltersFromEntries(
                    store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
                ).getValue(source).mode,
            )
        }

    @Test
    fun `compatibility mode outside legacy range is rejected without side effects`() = runBlocking {
        val store = FakeStore()
        var runtimeCalls = 0
        val coordinator = coordinator(
            store = store,
            runtime = { runtimeCalls++; true },
        )

        assertNull(coordinator.setCompatibilityFilter("org.example.chat", 3))
        assertEquals(0, store.writeCount)
        assertEquals(0, runtimeCalls)
    }

    @Test
    fun `persistence failure has no runtime or notification side effects`() = runBlocking {
        val source = "org.example.mail"
        val initial = encodeNotificationFilters(
            GLOBAL_NOTIFICATION_FILTER_PREFIX,
            mapOf(source to filter(enabled = false)),
        )
        val store = FakeStore(initial, failWrites = true)
        val calls = mutableListOf<String>()
        val coordinator = coordinator(
            store = store,
            runtime = { calls += "provider"; true },
            mute = { _, _ -> calls += "mute" },
            forget = { calls += "forget" },
        )
        coordinator.addListener { calls += "listener" }

        assertNull(coordinator.forgetCompatibilityFilter(source))
        assertEquals(initial, store.values)
        assertTrue(calls.isEmpty())
    }

    @Test
    fun `failed application retirement remains durable and startup retries then clears it`() =
        runBlocking {
            val source = "org.example.mail"
            val store = FakeStore(
                encodeCanonicalNotificationFilters(mapOf(source to filter(enabled = false)))
            )
            var failForget = true
            val forgotten = mutableListOf<String>()
            val coordinator = coordinator(
                store = store,
                forget = { application ->
                    forgotten += application
                    if (failForget) error("Room deletion failed")
                },
            )

            val result = coordinator.forgetCompatibilityFilter(source)

            assertNotNull(result)
            assertFalse(result.applicationStateUpdated)
            assertTrue(coordinator.needsReconciliation())
            assertEquals(setOf(source), notificationRetirementsFromEntries(
                store.entries(GLOBAL_NOTIFICATION_RETIREMENT_PREFIX),
            ))
            assertEquals(listOf(source), forgotten)

            failForget = false

            assertTrue(coordinator.reconcilePersistedState())
            assertEquals(listOf(source, source), forgotten)
            assertTrue(store.entries(GLOBAL_NOTIFICATION_RETIREMENT_PREFIX).isEmpty())
            assertFalse(coordinator.needsReconciliation())
        }

    @Test
    fun `re-enabling a source cancels its pending retirement`() = runBlocking {
        val source = "org.example.mail"
        val store = FakeStore(
            encodeCanonicalNotificationFilters(emptyMap()) +
                encodeNotificationRetirements(setOf(source)),
        )
        var forgetCalls = 0
        val coordinator = coordinator(
            store = store,
            forget = { forgetCalls++ },
        )

        assertNotNull(coordinator.setCompatibilityFilter(source, NOTIFICATION_ENABLED, "Mail"))

        assertTrue(store.entries(GLOBAL_NOTIFICATION_RETIREMENT_PREFIX).isEmpty())
        assertEquals(0, forgetCalls)
        assertEquals(
            filter(enabled = true, name = "Mail"),
            notificationFiltersFromEntries(store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX))[source],
        )
    }

    @Test
    fun `primary replacement and compatibility mutation cannot interleave`() = runBlocking {
        val source = "org.example.mail"
        val prefix = "watch.current.notifications."
        val store = FakeStore()
        val firstRuntimeEntered = CompletableDeferred<Unit>()
        val releaseFirstRuntime = CompletableDeferred<Unit>()
        var runtimeCalls = 0
        val coordinator = coordinator(
            store = store,
            runtime = {
                runtimeCalls++
                if (runtimeCalls == 1) {
                    firstRuntimeEntered.complete(Unit)
                    releaseFirstRuntime.await()
                }
                true
            },
        )

        val primary = async {
            coordinator.replacePrimary(
                prefix,
                encodeNotificationFilters(prefix, mapOf(source to filter(false))),
                mapOf(source to filter(false)),
            )
        }
        firstRuntimeEntered.await()
        val compatibility = async { coordinator.setCompatibilityFilter(source, 2) }
        repeat(10) { yield() }

        assertEquals(1, store.writeCount)
        assertEquals(1, runtimeCalls)
        releaseFirstRuntime.complete(Unit)
        assertNotNull(primary.await())
        assertNotNull(compatibility.await())
        assertEquals(2, store.writeCount)
        assertEquals(2, runtimeCalls)
        assertEquals(
            2,
            notificationFiltersFromEntries(
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
            ).getValue(source).mode,
        )
    }

    @Test
    fun `primary replacement keeps its watch projection and retires other fallbacks`() =
        runBlocking {
            val prefix = "watch.current.notifications."
            val source = "org.example.mail"
            val configured = mapOf(source to filter(false, "Mail", "email"))
            val values = encodeNotificationFilters(prefix, configured)
            val store = FakeStore(
                mapOf(
                    "watch.old.notifications.legacy.source" to "org.example.legacy",
                    "watch.old.notifications.legacy.enabled" to "1",
                )
            )
            val coordinator = coordinator(store)

            assertNotNull(coordinator.replacePrimary(prefix, values, configured))
            assertEquals(values, store.entries(prefix))
            assertTrue(store.entries("watch.old.notifications.").isEmpty())
            assertEquals(
                configured,
                notificationFiltersFromEntries(
                    store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
                ),
            )
        }

    @Test
    fun `primary enabled replacement preserves only conditional suppression`() = runBlocking {
        val prefix = "watch.current.notifications."
        val source = "org.example.mail"
        val conditional = PlatformNotificationFilter(
            enabled = true,
            name = "Mail",
            icon = "email",
            suppressWhenActive = true,
        )
        val requested = mapOf(source to filter(true, "Updated Mail", "new-email"))
        val store = FakeStore(
            encodeNotificationFilters(
                GLOBAL_NOTIFICATION_FILTER_PREFIX,
                mapOf(source to conditional),
            )
        )
        var runtime = emptyMap<String, PlatformNotificationFilter>()
        val coordinator = coordinator(
            store = store,
            runtime = { filters -> runtime = filters; true },
        )

        assertNotNull(
            coordinator.replacePrimary(
                prefix,
                encodeNotificationFilters(prefix, requested),
                requested,
            )
        )

        val expected = PlatformNotificationFilter(
            enabled = true,
            name = "Updated Mail",
            icon = "new-email",
            suppressWhenActive = true,
        )
        assertEquals(
            expected,
            notificationFiltersFromEntries(
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
            ).getValue(source),
        )
        assertEquals(
            expected,
            notificationFiltersFromEntries(store.entries(prefix)).getValue(source),
        )
        assertEquals(expected, runtime.getValue(source))
    }

    @Test
    fun `primary disabled replacement changes conditional compatibility mode to disabled`() =
        runBlocking {
            val prefix = "watch.current.notifications."
            val source = "org.example.mail"
            val conditional = PlatformNotificationFilter(
                enabled = true,
                name = "Mail",
                icon = "email",
                suppressWhenActive = true,
            )
            val requested = mapOf(source to filter(false, "Mail", "email"))
            val store = FakeStore(
                encodeNotificationFilters(
                    GLOBAL_NOTIFICATION_FILTER_PREFIX,
                    mapOf(source to conditional),
                )
            )
            var runtime = emptyMap<String, PlatformNotificationFilter>()
            val coordinator = coordinator(
                store = store,
                runtime = { filters -> runtime = filters; true },
            )

            assertNotNull(
                coordinator.replacePrimary(
                    prefix,
                    encodeNotificationFilters(prefix, requested),
                    requested,
                )
            )

            assertEquals(
                0,
                notificationFiltersFromEntries(
                    store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
                ).getValue(source).mode,
            )
            assertEquals(
                0,
                notificationFiltersFromEntries(store.entries(prefix)).getValue(source).mode,
            )
            assertEquals(0, runtime.getValue(source).mode)
        }

    @Test
    fun `primary enabled replacement keeps always enabled mode`() = runBlocking {
        val prefix = "watch.current.notifications."
        val source = "org.example.mail"
        val requested = mapOf(source to filter(true, "Mail", "email"))
        val store = FakeStore(
            encodeNotificationFilters(GLOBAL_NOTIFICATION_FILTER_PREFIX, requested)
        )
        var runtime = emptyMap<String, PlatformNotificationFilter>()
        val coordinator = coordinator(
            store = store,
            runtime = { filters -> runtime = filters; true },
        )

        assertNotNull(
            coordinator.replacePrimary(
                prefix,
                encodeNotificationFilters(prefix, requested),
                requested,
            )
        )

        assertEquals(
            2,
            notificationFiltersFromEntries(
                store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
            ).getValue(source).mode,
        )
        assertEquals(
            2,
            notificationFiltersFromEntries(store.entries(prefix)).getValue(source).mode,
        )
        assertEquals(2, runtime.getValue(source).mode)
    }

    @Test
    fun `primary replacement removes an omitted conditional source`() = runBlocking {
        val prefix = "watch.current.notifications."
        val retained = "org.example.mail"
        val omitted = "org.example.chat"
        val initial = mapOf(
            retained to filter(true, "Mail", "mail"),
            omitted to PlatformNotificationFilter(
                enabled = true,
                name = "Chat",
                icon = "chat",
                suppressWhenActive = true,
            ),
        )
        val requested = mapOf(retained to filter(true, "Mail", "mail"))
        val store = FakeStore(
            encodeNotificationFilters(GLOBAL_NOTIFICATION_FILTER_PREFIX, initial)
        )
        var runtime = emptyMap<String, PlatformNotificationFilter>()
        val muteStates = mutableListOf<Pair<String, MuteState>>()
        val coordinator = coordinator(
            store = store,
            runtime = { filters -> runtime = filters; true },
            mute = { source, state -> muteStates += source to state },
        )

        assertNotNull(
            coordinator.replacePrimary(
                prefix,
                encodeNotificationFilters(prefix, requested),
                requested,
            )
        )

        assertEquals(requested, runtime)
        assertEquals(requested, notificationFiltersFromEntries(
            store.entries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
        ))
        assertEquals(requested, notificationFiltersFromEntries(store.entries(prefix)))
        assertTrue(omitted !in runtime)
        assertEquals(omitted to MuteState.Never, muteStates.first())
    }

    @Test
    fun `a held mute update blocks a following mutation`() = runBlocking {
        val first = "org.example.mail"
        val second = "org.example.chat"
        val store = FakeStore()
        val firstMuteEntered = CompletableDeferred<Unit>()
        val releaseFirstMute = CompletableDeferred<Unit>()
        val muteStates = mutableListOf<Pair<String, MuteState>>()
        val coordinator = coordinator(
            store = store,
            mute = { source, state ->
                muteStates += source to state
                if (source == first) {
                    firstMuteEntered.complete(Unit)
                    releaseFirstMute.await()
                }
            },
        )

        val firstMutation = async { coordinator.setCompatibilityFilter(first, 0) }
        firstMuteEntered.await()
        val secondMutation = async { coordinator.setCompatibilityFilter(second, 0) }
        repeat(10) { yield() }

        assertEquals(1, store.writeCount)
        assertEquals(listOf(first to MuteState.Always), muteStates)
        assertFalse(firstMutation.isCompleted)
        assertFalse(secondMutation.isCompleted)

        releaseFirstMute.complete(Unit)
        assertNotNull(firstMutation.await())
        assertNotNull(secondMutation.await())
        assertEquals(2, store.writeCount)
        assertEquals(first to MuteState.Always, muteStates.first())
        assertEquals(3, muteStates.size)
        assertEquals(
            mapOf(first to MuteState.Always, second to MuteState.Always),
            muteStates.toMap(),
        )
    }

    @Test
    fun `startup reconciliation replays canonical filters and their mute states`() = runBlocking {
        val disabled = "org.example.mail"
        val conditional = "org.example.chat"
        val configured = mapOf(
            disabled to filter(false, "Mail", "mail"),
            conditional to PlatformNotificationFilter(
                enabled = true,
                name = "Chat",
                icon = "chat",
                suppressWhenActive = true,
            ),
        )
        val store = FakeStore(
            encodeNotificationFilters(GLOBAL_NOTIFICATION_FILTER_PREFIX, configured)
        )
        var runtime: Map<String, PlatformNotificationFilter>? = null
        var reconciledMuteStates: Map<String, MuteState>? = null
        val coordinator = coordinator(
            store = store,
            runtime = { filters -> runtime = filters; true },
            reconcileMuteStates = { states -> reconciledMuteStates = states },
        )

        assertTrue(coordinator.reconcilePersistedState())

        assertEquals(configured, runtime)
        assertEquals(
            mapOf(disabled to MuteState.Always, conditional to MuteState.Never),
            reconciledMuteStates,
        )
        assertEquals(0, store.writeCount)
    }

    @Test
    fun `startup reconciliation does not apply mute states when runtime replay fails`() = runBlocking {
        val source = "org.example.mail"
        val store = FakeStore(
            encodeNotificationFilters(
                GLOBAL_NOTIFICATION_FILTER_PREFIX,
                mapOf(source to filter(false, "Mail", "mail")),
            )
        )
        var reconciledMuteStates: Map<String, MuteState>? = null
        val coordinator = coordinator(
            store = store,
            runtime = { error("provider replay failed") },
            reconcileMuteStates = { states -> reconciledMuteStates = states },
        )

        assertFalse(coordinator.reconcilePersistedState())

        assertNull(reconciledMuteStates)
        assertEquals(0, store.writeCount)
    }

    @Test
    fun `startup retirement timeout keeps durable intent for retry`() = runBlocking {
        val source = "org.example.mail"
        val store = FakeStore(
            encodeCanonicalNotificationFilters(emptyMap()) +
                encodeNotificationRetirements(setOf(source)),
        )
        val coordinator = coordinator(
            store = store,
            forget = { awaitCancellation() },
            muteStateTimeoutMs = 25,
        )

        assertFalse(withTimeout(1_000) { coordinator.reconcilePersistedState() })
        assertTrue(coordinator.needsReconciliation())
        assertEquals(
            setOf(source),
            notificationRetirementsFromEntries(store.entries(GLOBAL_NOTIFICATION_RETIREMENT_PREFIX)),
        )
    }

    private fun coordinator(
        store: FakeStore,
        runtime: suspend (Map<String, PlatformNotificationFilter>) -> Boolean = { true },
        mute: suspend (String, MuteState) -> Unit = { _, _ -> },
        forget: suspend (String) -> Unit = {},
        reconcileMuteStates: suspend (Map<String, MuteState>) -> Unit = {},
        muteStateTimeoutMs: Long = 5_000,
    ) = NotificationFilterCoordinator(
        loadEntries = store::entries,
        replacePrefixes = store::replacePrefixes,
        replaceRuntimeFilters = runtime,
        updateMuteState = mute,
        forgetApplication = forget,
        reconcileMuteStates = reconcileMuteStates,
        muteStateTimeoutMs = muteStateTimeoutMs,
    )

    private fun filter(
        enabled: Boolean,
        name: String = "Mail",
        icon: String = "",
    ) = PlatformNotificationFilter(enabled, name, icon)

    private class FakeStore(
        initial: Map<String, String> = emptyMap(),
        private val failWrites: Boolean = false,
    ) {
        val values = initial.toMutableMap()
        var writeCount = 0

        fun entries(prefix: String): Map<String, String> =
            values.filterKeys { it.startsWith(prefix) }

        fun replacePrefixes(replacements: Map<String, Map<String, String>>): Boolean {
            writeCount++
            if (failWrites) return false
            replacements.keys.forEach { prefix ->
                values.keys.filter { it.startsWith(prefix) }.toList().forEach(values::remove)
            }
            replacements.values.forEach(values::putAll)
            return true
        }
    }
}
