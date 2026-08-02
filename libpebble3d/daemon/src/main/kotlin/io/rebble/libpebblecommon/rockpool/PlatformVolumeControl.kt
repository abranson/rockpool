/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.linux.music.VolumeControl
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/** System-volume-only Sailfish adapter; metadata and transport stay on MPRIS. */
internal class PlatformVolumeControl(
    private val controller: PlatformProviderController,
    private val commandScope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    private val executeCommand: suspend (Int, () -> Boolean) -> Int? =
        controller::mediaCommand,
) : VolumeControl {
    private data class PendingCommand(val epoch: Long, val command: Int)

    private val logger = Logger.withTag("PlatformVolumeControl")
    private val stateLock = Any()
    private val mutableVolumePercent = MutableStateFlow<Int?>(null)
    private val commands = Channel<PendingCommand>(MAX_PENDING_COMMANDS)
    private var mediaEpoch = 0L

    override val volumePercent: StateFlow<Int?> = mutableVolumePercent

    init {
        controller.addMediaListener { volume ->
            synchronized(stateLock) {
                if (volume == null && mutableVolumePercent.value != null) {
                    mediaEpoch++
                }
                mutableVolumePercent.value = volume
            }
        }
        commandScope.launch {
            for (pending in commands) {
                val current = synchronized(stateLock) {
                    pending.epoch == mediaEpoch && mutableVolumePercent.value != null
                }
                if (!current) continue
                try {
                    val status = executeCommand(pending.command) {
                        synchronized(stateLock) {
                            pending.epoch == mediaEpoch &&
                                mutableVolumePercent.value != null
                        }
                    }
                    if (status == null) continue
                    if (status != STATUS_OK) {
                        logger.w { "platform media command ${pending.command} failed: $status" }
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "platform media command ${pending.command} failed" }
                }
            }
        }
    }

    /** The provider pushes both its initial state and later mixer changes. */
    override fun refresh() = Unit

    override fun step(delta: Int) {
        val command = when {
            delta > 0 -> PlatformProviderController.MEDIA_VOLUME_UP
            delta < 0 -> PlatformProviderController.MEDIA_VOLUME_DOWN
            else -> return
        }
        val pending = synchronized(stateLock) {
            if (mutableVolumePercent.value == null) null else PendingCommand(mediaEpoch, command)
        } ?: return
        if (commands.trySend(pending).isFailure) {
            logger.w { "platform media command queue is full; dropping $command" }
        }
    }

    private companion object {
        private const val STATUS_OK = 0
        private const val MAX_PENDING_COMMANDS = 32
    }
}
