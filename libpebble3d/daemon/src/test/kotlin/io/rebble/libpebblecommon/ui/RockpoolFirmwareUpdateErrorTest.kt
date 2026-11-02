package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdateErrorStarting
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater.FirmwareUpdateStatus
import kotlin.test.Test
import kotlin.test.assertEquals

class RockpoolFirmwareUpdateErrorTest {
    @Test
    fun `errors expose bounded retry codes rather than internal exception details`() {
        assertEquals("download-failed", rockpoolFirmwareUpdateError(
            FirmwareUpdateStatus.NotInProgress.ErrorStarting(FirmwareUpdateErrorStarting.ErrorDownloading),
        ))
        assertEquals("invalid-firmware", rockpoolFirmwareUpdateError(
            FirmwareUpdateStatus.NotInProgress.ErrorStarting(FirmwareUpdateErrorStarting.ErrorParsingPbz),
        ))
        assertEquals("update-failed", rockpoolFirmwareUpdateError(
            FirmwareUpdateStatus.NotInProgress.Idle(Exception("private transport details")),
        ))
        assertEquals("", rockpoolFirmwareUpdateError(FirmwareUpdateStatus.NotInProgress.Idle()))
        assertEquals("", rockpoolFirmwareUpdateError(null))
    }
}
