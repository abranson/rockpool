/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater
import io.rebble.libpebblecommon.connection.endpointmanager.LanguagePackInstallState
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first

internal enum class PrimaryInstallCompletion {
    SUCCEEDED,
    FAILED,
    DISCONNECTED,
}

internal suspend fun awaitPrimaryFirmwareInstall(
    initial: FirmwareUpdater.FirmwareUpdateStatus,
    states: Flow<FirmwareUpdater.FirmwareUpdateStatus?>,
    trigger: () -> Unit,
): PrimaryInstallCompletion = coroutineScope {
    var started = false
    val completion = async(start = CoroutineStart.UNDISPATCHED) {
        states.first { state ->
            when (state) {
                is FirmwareUpdater.FirmwareUpdateStatus.WaitingToStart,
                is FirmwareUpdater.FirmwareUpdateStatus.InProgress,
                -> {
                    started = true
                    false
                }

                is FirmwareUpdater.FirmwareUpdateStatus.WaitingForReboot -> {
                    started = true
                    true
                }

                is FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.ErrorStarting ->
                    started || state != initial

                is FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle ->
                    started && state.lastFailure != null

                null -> started
            }
        }
    }
    trigger()
    when (completion.await()) {
        is FirmwareUpdater.FirmwareUpdateStatus.WaitingForReboot ->
            PrimaryInstallCompletion.SUCCEEDED

        null -> PrimaryInstallCompletion.DISCONNECTED
        else -> PrimaryInstallCompletion.FAILED
    }
}

internal suspend fun awaitPrimaryLanguageInstall(
    initial: LanguagePackInstallState,
    states: Flow<LanguagePackInstallState?>,
    expectedName: String,
    trigger: () -> Unit,
): PrimaryInstallCompletion = coroutineScope {
    var started = false
    val completion = async(start = CoroutineStart.UNDISPATCHED) {
        states.first { state ->
            when (state) {
                is LanguagePackInstallState.Downloading,
                is LanguagePackInstallState.Installing,
                -> {
                    started = true
                    false
                }

                is LanguagePackInstallState.Idle ->
                    (started || state != initial) &&
                        (state.successfullyInstalledLanguage != null || state.previousError != null)

                null -> started
            }
        }
    }
    trigger()
    when (val terminal = completion.await()) {
        is LanguagePackInstallState.Idle ->
            if (terminal.successfullyInstalledLanguage == expectedName) {
                PrimaryInstallCompletion.SUCCEEDED
            } else {
                PrimaryInstallCompletion.FAILED
            }

        null -> PrimaryInstallCompletion.DISCONNECTED
        else -> PrimaryInstallCompletion.FAILED
    }
}
