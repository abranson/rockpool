/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class PlatformVolumeControlTest {
    @Test
    fun lateConstructionReplaysInitialProviderVolume() {
        val controller = PlatformProviderController()
        controller.providerMediaVolume(64)

        val volume = PlatformVolumeControl(controller) { _, isCurrent ->
            if (isCurrent()) 0 else null
        }

        assertEquals(64, volume.volumePercent.value)
    }

    @Test
    fun domainLossClearsSystemVolumeForMprisFallback() {
        val controller = PlatformProviderController()
        val volume = PlatformVolumeControl(controller) { _, isCurrent ->
            if (isCurrent()) 0 else null
        }
        controller.providerMediaVolume(64)

        controller.providerSnapshotChanged(
            PlatformProviderSnapshot(
                state = "degraded",
                domains = 0,
                supportedDomains = PlatformProviderController.MEDIA_DOMAIN,
                degradedDomains = PlatformProviderController.MEDIA_DOMAIN,
            )
        )

        assertNull(volume.volumePercent.value)
    }

    @Test
    fun `step returns while provider command is held`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val commandStarted = CompletableDeferred<Unit>()
            val releaseCommand = CompletableDeferred<Unit>()
            val controller = PlatformProviderController()
            val volume = PlatformVolumeControl(
                controller = controller,
                executeCommand = { _, isCurrent ->
                    if (!isCurrent()) {
                        null
                    } else {
                        commandStarted.complete(Unit)
                        releaseCommand.await()
                        0
                    }
                },
                commandScope = scope,
            )
            controller.providerMediaVolume(64)

            withTimeout(1_000) { async { volume.step(1) }.await() }
            withTimeout(1_000) { commandStarted.await() }
            releaseCommand.complete(Unit)
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `rapid signed steps preserve provider command order`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val firstCommandStarted = CompletableDeferred<Unit>()
            val releaseFirstCommand = CompletableDeferred<Unit>()
            val completed = CompletableDeferred<Unit>()
            val commands = mutableListOf<Int>()
            val controller = PlatformProviderController()
            val volume = PlatformVolumeControl(
                controller = controller,
                executeCommand = { command, isCurrent ->
                    if (!isCurrent()) {
                        null
                    } else {
                        commands += command
                        if (commands.size == 1) {
                            firstCommandStarted.complete(Unit)
                            releaseFirstCommand.await()
                        }
                        if (commands.size == 3) completed.complete(Unit)
                        0
                    }
                },
                commandScope = scope,
            )
            controller.providerMediaVolume(64)

            volume.step(-3)
            withTimeout(1_000) { firstCommandStarted.await() }
            volume.step(0)
            volume.step(2)
            volume.step(-1)
            assertEquals(listOf(PlatformProviderController.MEDIA_VOLUME_DOWN), commands)

            releaseFirstCommand.complete(Unit)
            withTimeout(1_000) { completed.await() }
            assertEquals(
                listOf(
                    PlatformProviderController.MEDIA_VOLUME_DOWN,
                    PlatformProviderController.MEDIA_VOLUME_UP,
                    PlatformProviderController.MEDIA_VOLUME_DOWN,
                ),
                commands,
            )
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `media-domain loss drops commands queued behind an in-flight step`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val firstCommandStarted = CompletableDeferred<Unit>()
            val releaseFirstCommand = CompletableDeferred<Unit>()
            val commands = mutableListOf<Int>()
            val controller = PlatformProviderController()
            val volume = PlatformVolumeControl(
                controller = controller,
                executeCommand = { command, isCurrent ->
                    if (!isCurrent()) {
                        null
                    } else {
                        commands += command
                        if (commands.size == 1) {
                            firstCommandStarted.complete(Unit)
                            releaseFirstCommand.await()
                        }
                        0
                    }
                },
                commandScope = scope,
            )
            controller.providerMediaVolume(64)

            volume.step(1)
            withTimeout(1_000) { firstCommandStarted.await() }
            volume.step(-1)
            volume.step(1)
            assertEquals(listOf(PlatformProviderController.MEDIA_VOLUME_UP), commands)

            controller.providerSnapshotChanged(
                PlatformProviderSnapshot(
                    state = "degraded",
                    domains = 0,
                    supportedDomains = PlatformProviderController.MEDIA_DOMAIN,
                    degradedDomains = PlatformProviderController.MEDIA_DOMAIN,
                ),
            )
            releaseFirstCommand.complete(Unit)

            assertEquals(listOf(PlatformProviderController.MEDIA_VOLUME_UP), commands)
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `media-domain loss after dequeue rejects command before native dispatch`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val commandDequeued = CompletableDeferred<Unit>()
            val releaseGate = CompletableDeferred<Unit>()
            val commandFinished = CompletableDeferred<Unit>()
            val commands = mutableListOf<Int>()
            val controller = PlatformProviderController()
            val volume = PlatformVolumeControl(
                controller = controller,
                executeCommand = { command, isCurrent ->
                    commandDequeued.complete(Unit)
                    releaseGate.await()
                    val status = if (isCurrent()) {
                        commands += command
                        0
                    } else {
                        null
                    }
                    commandFinished.complete(Unit)
                    status
                },
                commandScope = scope,
            )
            controller.providerMediaVolume(64)

            volume.step(1)
            withTimeout(1_000) { commandDequeued.await() }
            controller.providerSnapshotChanged(
                PlatformProviderSnapshot(
                    state = "degraded",
                    domains = 0,
                    supportedDomains = PlatformProviderController.MEDIA_DOMAIN,
                    degradedDomains = PlatformProviderController.MEDIA_DOMAIN,
                ),
            )
            // A replacement helper can publish volume before the old worker
            // reaches the lifecycle-locked generation check.
            controller.providerMediaVolume(48)
            releaseGate.complete(Unit)

            withTimeout(1_000) { commandFinished.await() }
            assertEquals(emptyList(), commands)
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `steps beyond the bounded queue are dropped while a command is held`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val firstCommandStarted = CompletableDeferred<Unit>()
            val releaseFirstCommand = CompletableDeferred<Unit>()
            val queuedCommandsCompleted = CompletableDeferred<Unit>()
            val commands = mutableListOf<Int>()
            val controller = PlatformProviderController()
            val volume = PlatformVolumeControl(
                controller = controller,
                executeCommand = { command, isCurrent ->
                    if (!isCurrent()) {
                        null
                    } else {
                        commands += command
                        if (commands.size == 1) {
                            firstCommandStarted.complete(Unit)
                            releaseFirstCommand.await()
                        }
                        if (commands.size == 33) queuedCommandsCompleted.complete(Unit)
                        0
                    }
                },
                commandScope = scope,
            )
            controller.providerMediaVolume(64)

            volume.step(1)
            withTimeout(1_000) { firstCommandStarted.await() }
            repeat(40) { volume.step(1) }
            releaseFirstCommand.complete(Unit)

            withTimeout(1_000) { queuedCommandsCompleted.await() }
            assertEquals(33, commands.size)
            assertEquals(
                List(33) { PlatformProviderController.MEDIA_VOLUME_UP },
                commands,
            )
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `failed provider command does not prevent later commands`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val completed = CompletableDeferred<Unit>()
            val commands = mutableListOf<Int>()
            val controller = PlatformProviderController()
            val volume = PlatformVolumeControl(
                controller = controller,
                executeCommand = { command, isCurrent ->
                    if (!isCurrent()) {
                        null
                    } else {
                        commands += command
                        if (commands.size == 1) error("provider unavailable")
                        completed.complete(Unit)
                        0
                    }
                },
                commandScope = scope,
            )
            controller.providerMediaVolume(64)

            volume.step(1)
            volume.step(-1)

            withTimeout(1_000) { completed.await() }
            assertEquals(
                listOf(
                    PlatformProviderController.MEDIA_VOLUME_UP,
                    PlatformProviderController.MEDIA_VOLUME_DOWN,
                ),
                commands,
            )
        } finally {
            scope.cancel()
        }
        Unit
    }
}
