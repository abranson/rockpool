/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckResult
import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckState
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first

internal data class PrimaryFirmwareStatus(
    val recovery: Boolean,
    val checking: Boolean,
    val updateAvailable: Boolean,
    val candidateVersion: String,
    val releaseNotes: String,
    val updateState: String,
    val updateProgress: Double,
)

internal fun primaryFirmwareStatus(
    connected: Boolean,
    recovery: Boolean,
    check: FirmwareUpdateCheckState?,
    update: FirmwareUpdater.FirmwareUpdateStatus?,
): PrimaryFirmwareStatus {
    val candidate = check?.result as? FirmwareUpdateCheckResult.FoundUpdate
    val state = when {
        !connected -> "disconnected"
        update is FirmwareUpdater.FirmwareUpdateStatus.WaitingToStart -> "waiting-to-start"
        update is FirmwareUpdater.FirmwareUpdateStatus.InProgress -> "installing"
        update is FirmwareUpdater.FirmwareUpdateStatus.WaitingForReboot -> "waiting-for-reboot"
        check?.checkingForUpdates == true -> "checking"
        update is FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.ErrorStarting -> "failed"
        update is FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle &&
            update.lastFailure != null -> "failed"
        check?.result is FirmwareUpdateCheckResult.FoundUpdate -> "available"
        check?.result is FirmwareUpdateCheckResult.FoundNoUpdate -> "up-to-date"
        check?.result is FirmwareUpdateCheckResult.UpdateCheckFailed -> "check-failed"
        else -> "idle"
    }
    val progress = (update as? FirmwareUpdater.FirmwareUpdateStatus.InProgress)
        ?.progress?.value?.toDouble()?.coerceIn(0.0, 1.0) ?: 0.0
    return PrimaryFirmwareStatus(
        recovery = recovery,
        checking = check?.checkingForUpdates == true,
        updateAvailable = candidate != null,
        candidateVersion = candidate?.version?.stringVersion
            ?.boundedFirmwareText(MAX_FIRMWARE_VERSION_BYTES).orEmpty(),
        releaseNotes = candidate?.notes
            ?.boundedFirmwareText(MAX_FIRMWARE_RELEASE_NOTES_BYTES).orEmpty(),
        updateState = state,
        updateProgress = progress,
    )
}

internal suspend fun awaitFirmwareCheck(
    initial: FirmwareUpdateCheckState,
    states: Flow<FirmwareUpdateCheckState>,
    trigger: () -> Unit,
): FirmwareUpdateCheckState {
    if (initial.checkingForUpdates) return states.first { !it.checkingForUpdates }
    return coroutineScope {
        // Subscribe before triggering: a cached/fast provider may publish checking=true and the
        // terminal result before the caller gets another dispatch turn.
        val completion = async(start = CoroutineStart.UNDISPATCHED) {
            var started = false
            states.first { state ->
                if (state.checkingForUpdates) {
                    started = true
                    false
                } else {
                    started
                }
            }
        }
        trigger()
        completion.await()
    }
}

private fun String.boundedFirmwareText(maxBytes: Int): String? =
    takeIf { '\u0000' !in it && it.encodeToByteArray().size <= maxBytes }

private const val MAX_FIRMWARE_VERSION_BYTES = 128
private const val MAX_FIRMWARE_RELEASE_NOTES_BYTES = 16 * 1024
