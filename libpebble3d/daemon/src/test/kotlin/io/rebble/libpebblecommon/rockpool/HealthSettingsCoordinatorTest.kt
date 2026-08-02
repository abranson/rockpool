/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.health.HealthSettings
import io.rebble.libpebblecommon.health.HealthSettingsInitialization
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.milliseconds

class HealthSettingsCoordinatorTest {
    @Test
    fun `fresh initialization avoids existing-row decoding and notifies listeners`() = runBlocking {
        val base = FakeLibPebble()
        val defaults = base.healthSettings.first()
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = flow {
                error("existing health rows must not be decoded before the raw absent check")
            }
        }
        val coordinator = HealthSettingsCoordinator(libPebble) {
            error("fresh initialization must not use the ordinary writer")
        }
        val changes = mutableListOf<HealthSettingsUpdate>()
        coordinator.addListener(changes::add)

        val result = coordinator.initializeIfAbsent(
            transform = { it.copy(ageYears = 53, imperialUnits = true) },
            initialize = { transform ->
                val initialized = transform(defaults)
                HealthSettingsInitialization(defaults, initialized)
            },
        )

        assertEquals(HealthSettingsInitializationResult.Initialized, result)
        assertEquals(1, changes.size)
        assertEquals(53, changes.single().current.ageYears)
        assertTrue(changes.single().current.imperialUnits)
    }

    @Test
    fun `accepted health write excludes background initialization until it settles`() = runBlocking {
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first())
        val writeStarted = CompletableDeferred<HealthSettings>()
        val releaseWrite = CompletableDeferred<Unit>()
        val initializerEntered = CompletableDeferred<Unit>()
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val coordinator = HealthSettingsCoordinator(libPebble) { healthSettings ->
            writeStarted.complete(healthSettings)
            releaseWrite.await()
            state.value = healthSettings
        }
        val write = async {
            coordinator.updateWithResultSuspend(
                transform = { it.copy(ageYears = 47) },
                commit = { true },
            )
        }
        writeStarted.await()
        val initialization = async {
            coordinator.initializeIfAbsent(
                transform = { it.copy(imperialUnits = true) },
                initialize = {
                    initializerEntered.complete(Unit)
                    null
                },
            )
        }
        yield()

        assertFalse(initialization.isCompleted)
        assertFalse(initializerEntered.isCompleted)

        releaseWrite.complete(Unit)
        assertTrue(write.await().saved)
        assertEquals(HealthSettingsInitializationResult.Existing, initialization.await())
        assertTrue(initializerEntered.isCompleted)
    }

    @Test
    fun `primary and compatibility updates preserve each other's fields`() = runBlocking {
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first())
        val firstWrite = CompletableDeferred<HealthSettings>()
        val writes = AtomicInteger()
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val releaseFirst = CompletableDeferred<Unit>()
        val coordinator = HealthSettingsCoordinator(libPebble) { healthSettings ->
            if (writes.incrementAndGet() == 1) {
                firstWrite.complete(healthSettings)
                releaseFirst.await()
            }
            state.value = healthSettings
        }
        val changes = mutableListOf<HealthSettingsUpdate>()
        coordinator.addListener(changes::add)

        val primary = async {
            coordinator.updateWithResultSuspend(
                transform = { it.copy(ageYears = 42) },
                commit = { true },
            )
        }
        firstWrite.await()
        val compatibility = async {
            coordinator.updateWithResultSuspend(
                transform = { it.copy(imperialUnits = true) },
                commit = { true },
            )
        }
        yield()

        assertFalse(compatibility.isCompleted)
        assertEquals(1, writes.get())

        releaseFirst.complete(Unit)
        assertEquals(true, primary.await().saved)
        assertEquals(true, compatibility.await().saved)
        assertEquals(2, writes.get())
        assertEquals(42, state.value.ageYears)
        assertEquals(true, state.value.imperialUnits)
        assertEquals(listOf(42, 42), changes.map { it.current.ageYears })
        assertEquals(listOf(false, true), changes.map { it.current.imperialUnits })
    }

    @Test
    fun `cancelled commit neither writes nor notifies`() = runBlocking {
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first())
        var writes = 0
        var notifications = 0
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val coordinator = HealthSettingsCoordinator(libPebble) { healthSettings ->
            writes++
            state.value = healthSettings
        }
        coordinator.addListener { notifications++ }

        val result = coordinator.updateWithResultSuspend(
            transform = { it.copy(ageYears = 51) },
            commit = { false },
        )

        assertFalse(result.saved)
        assertEquals(0, writes)
        assertEquals(0, notifications)
        assertEquals(base.healthSettings.first(), state.value)
    }

    @Test
    fun `timed out durable write is cancelled before a later write starts`() = runBlocking {
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first())
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        var attempts = 0
        var firstWriteActive = false
        var notifications = 0
        val coordinator = HealthSettingsCoordinator(
            libPebble = libPebble,
            persistHealthSettings = { healthSettings ->
                attempts++
                if (attempts == 1) {
                    firstWriteActive = true
                    try {
                        awaitCancellation()
                    } finally {
                        firstWriteActive = false
                    }
                }
                state.value = healthSettings
            },
            healthWriteTimeout = 50.milliseconds,
        )
        coordinator.addListener { notifications++ }

        assertFailsWith<IllegalStateException> {
            coordinator.updateWithResultSuspend(
                transform = { it.copy(ageYears = 50) },
                commit = { true },
            )
        }
        assertFalse(firstWriteActive)
        assertEquals(0, notifications)

        val second = coordinator.updateWithResultSuspend(
            transform = { it.copy(ageYears = 51) },
            commit = { true },
        )
        assertTrue(second.saved)
        assertEquals(51, state.value.ageYears)
        assertEquals(1, notifications)
    }

    @Test
    fun `watch-origin settings change notifies listeners once`() = runBlocking {
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first())
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val coordinator = HealthSettingsCoordinator(libPebble) { state.value = it }
        val observerScope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val change = CompletableDeferred<HealthSettingsUpdate>()
        coordinator.addListener { change.complete(it) }

        try {
            coordinator.startObserving(observerScope)
            yield()
            state.value = state.value.copy(ageYears = 61)

            val observed = withTimeout(1_000) { change.await() }
            assertEquals(35, observed.previous.ageYears)
            assertEquals(61, observed.current.ageYears)
        } finally {
            observerScope.cancel()
        }
    }
}
