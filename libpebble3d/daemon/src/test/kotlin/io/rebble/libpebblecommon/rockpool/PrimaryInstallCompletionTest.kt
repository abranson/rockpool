/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckResult
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater
import io.rebble.libpebblecommon.connection.endpointmanager.LanguagePackInstallState
import io.rebble.libpebblecommon.services.FirmwareVersion
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.time.Instant

class PrimaryInstallCompletionTest {
    @Test
    fun `firmware completion subscribes before a fast update`() = runBlocking {
        val states = MutableSharedFlow<FirmwareUpdater.FirmwareUpdateStatus?>(extraBufferCapacity = 3)
        val initial = FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle()

        val result = awaitPrimaryFirmwareInstall(initial, states) {
            assertTrue(states.tryEmit(FirmwareUpdater.FirmwareUpdateStatus.WaitingToStart(update)))
            assertTrue(
                states.tryEmit(
                    FirmwareUpdater.FirmwareUpdateStatus.InProgress(
                        update,
                        MutableStateFlow(0.5f),
                    ),
                ),
            )
            assertTrue(states.tryEmit(FirmwareUpdater.FirmwareUpdateStatus.WaitingForReboot(update)))
        }

        assertEquals(PrimaryInstallCompletion.SUCCEEDED, result)
    }

    @Test
    fun `firmware start failure is reported`() = runBlocking {
        val states = MutableSharedFlow<FirmwareUpdater.FirmwareUpdateStatus?>(extraBufferCapacity = 1)
        val initial = FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle()

        val result = awaitPrimaryFirmwareInstall(initial, states) {
            assertTrue(
                states.tryEmit(
                    FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.ErrorStarting(
                        io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdateErrorStarting.ErrorParsingPbz,
                    ),
                ),
            )
        }

        assertEquals(PrimaryInstallCompletion.FAILED, result)
    }

    @Test
    fun `language completion requires the requested install name`() = runBlocking {
        val states = MutableSharedFlow<LanguagePackInstallState?>(extraBufferCapacity = 2)
        val initial = LanguagePackInstallState.Idle()

        val result = awaitPrimaryLanguageInstall(initial, states, "primary-language-pack") {
            assertTrue(states.tryEmit(LanguagePackInstallState.Downloading("primary-language-pack")))
            assertTrue(
                states.tryEmit(
                    LanguagePackInstallState.Idle(
                        successfullyInstalledLanguage = "primary-language-pack",
                    ),
                ),
            )
        }

        assertEquals(PrimaryInstallCompletion.SUCCEEDED, result)
    }

    private companion object {
        val update = FirmwareUpdateCheckResult.FoundUpdate(
            version = FirmwareVersion(
                stringVersion = "v1.0.0-test",
                timestamp = Instant.DISTANT_PAST,
                major = 1,
                minor = 0,
                patch = 0,
                suffix = "test",
                gitHash = "",
                isRecovery = false,
                isDualSlot = false,
                isSlot0 = false,
            ),
            url = "",
            notes = "Sideloaded",
        )
    }
}
