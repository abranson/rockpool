/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdateErrorStarting
import io.rebble.libpebblecommon.connection.endpointmanager.FirmwareUpdater.FirmwareUpdateStatus

// Stable UI codes, without exception messages or download URLs.
internal fun rockpoolFirmwareUpdateError(state: FirmwareUpdateStatus?): String = when (state) {
    is FirmwareUpdateStatus.NotInProgress.ErrorStarting -> when (state.error) {
        FirmwareUpdateErrorStarting.ErrorDownloading -> "download-failed"
        FirmwareUpdateErrorStarting.ErrorParsingPbz -> "invalid-firmware"
    }
    is FirmwareUpdateStatus.NotInProgress.Idle -> if (state.lastFailure != null) "update-failed" else ""
    else -> ""
}
