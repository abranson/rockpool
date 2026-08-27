/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckResult
import io.rebble.libpebblecommon.connection.FirmwareUpdateCheckState
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater
import io.rebble.libpebblecommon.services.FirmwareVersion
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.time.Instant

class PrimaryFirmwareTest {
    @Test
    fun `firmware check observes a fast complete transition after subscribing`() = runBlocking {
        val states = MutableSharedFlow<FirmwareUpdateCheckState>(extraBufferCapacity = 2)
        val terminal = FirmwareUpdateCheckState(
            false,
            FirmwareUpdateCheckResult.FoundNoUpdate,
        )
        var triggered = 0

        val result = awaitFirmwareCheck(
            FirmwareUpdateCheckState(false, null),
            states,
        ) {
            triggered++
            assertTrue(states.tryEmit(FirmwareUpdateCheckState(true, null)))
            assertTrue(states.tryEmit(terminal))
        }

        assertEquals(1, triggered)
        assertEquals(terminal, result)
    }

    @Test
    fun `firmware check joins an existing request without retriggering`() = runBlocking {
        val states = MutableSharedFlow<FirmwareUpdateCheckState>(replay = 1)
        val terminal = FirmwareUpdateCheckState(
            false,
            FirmwareUpdateCheckResult.FoundNoUpdate,
        )
        states.emit(terminal)
        var triggered = false

        val result = awaitFirmwareCheck(
            FirmwareUpdateCheckState(true, null),
            states,
        ) {
            triggered = true
        }

        assertFalse(triggered)
        assertEquals(terminal, result)
    }

    @Test
    fun `candidate metadata and install progress map without exposing download URL`() {
        val candidate = candidate(notes = "Important fixes")
        val status = primaryFirmwareStatus(
            connected = true,
            recovery = true,
            check = FirmwareUpdateCheckState(false, candidate),
            update = FirmwareUpdater.FirmwareUpdateStatus.InProgress(
                candidate,
                MutableStateFlow(1.5f),
            ),
        )

        assertTrue(status.recovery)
        assertTrue(status.updateAvailable)
        assertEquals("v4.9.9-primary", status.candidateVersion)
        assertEquals("Important fixes", status.releaseNotes)
        assertEquals("installing", status.updateState)
        assertEquals(1.0, status.updateProgress)
        assertFalse(status.toString().contains("secret-download"))
    }

    @Test
    fun `check and disconnected states remain explicit`() {
        val checking = primaryFirmwareStatus(
            connected = true,
            recovery = false,
            check = FirmwareUpdateCheckState(true, null),
            update = FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle(),
        )
        val failed = primaryFirmwareStatus(
            connected = true,
            recovery = false,
            check = FirmwareUpdateCheckState(
                false,
                FirmwareUpdateCheckResult.UpdateCheckFailed("private failure"),
            ),
            update = FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle(),
        )
        val disconnected = primaryFirmwareStatus(false, false, null, null)

        assertEquals("checking", checking.updateState)
        assertTrue(checking.checking)
        assertEquals("check-failed", failed.updateState)
        assertEquals("disconnected", disconnected.updateState)
    }

    @Test
    fun `oversized or malformed remote candidate text is omitted`() {
        val status = primaryFirmwareStatus(
            connected = true,
            recovery = false,
            check = FirmwareUpdateCheckState(
                false,
                candidate(notes = "x".repeat(16 * 1024 + 1), version = "bad\u0000version"),
            ),
            update = FirmwareUpdater.FirmwareUpdateStatus.NotInProgress.Idle(),
        )

        assertEquals("", status.candidateVersion)
        assertEquals("", status.releaseNotes)
        assertTrue(status.updateAvailable)
    }

    private fun candidate(
        notes: String,
        version: String = "v4.9.9-primary",
    ): FirmwareUpdateCheckResult.FoundUpdate = FirmwareUpdateCheckResult.FoundUpdate(
        version = FirmwareVersion(
            stringVersion = version,
            timestamp = Instant.DISTANT_PAST,
            major = 4,
            minor = 9,
            patch = 9,
            suffix = "primary",
            gitHash = "",
            isRecovery = false,
            isDualSlot = false,
            isSlot0 = false,
        ),
        url = "https://secret-download.invalid/firmware.pbz",
        notes = notes,
    )
}
