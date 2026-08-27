/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.BondedWatchImportOutcome
import java.io.IOException

/**
 * D-Bus-facing interpretation of the portable importer result.  Keeping this
 * separate from [LibPebble3Service] makes the commit/cancellation contract
 * independently testable without exporting a D-Bus service.
 */
internal sealed interface BondedWatchImportOperationResult {
    data class Completed(
        val imported: Int,
        val existing: Int,
        val aliases: Int,
        val unsupported: Int,
    ) : BondedWatchImportOperationResult

    data class Failed(
        val error: String,
        val detail: String,
    ) : BondedWatchImportOperationResult
}

internal suspend fun runBondedWatchImport(
    importer: suspend (beginCommit: () -> Boolean) -> BondedWatchImportOutcome,
    beginCommit: () -> Boolean,
): BondedWatchImportOperationResult = try {
    when (val outcome = importer(beginCommit)) {
        is BondedWatchImportOutcome.Completed -> BondedWatchImportOperationResult.Completed(
            imported = outcome.imported,
            existing = outcome.existing,
            aliases = outcome.aliases,
            unsupported = outcome.unsupported,
        )
        BondedWatchImportOutcome.Unavailable -> BondedWatchImportOperationResult.Failed(
            ERROR_TRANSPORT_FAILED,
            "bond enumeration is unavailable",
        )
        BondedWatchImportOutcome.Cancelled -> BondedWatchImportOperationResult.Failed(
            ERROR_CANCELLED,
            "bond import was cancelled",
        )
        BondedWatchImportOutcome.PersistenceFailed -> BondedWatchImportOperationResult.Failed(
            ERROR_IO,
            "bond import storage failed",
        )
    }
} catch (_: IOException) {
    BondedWatchImportOperationResult.Failed(ERROR_IO, "bond import storage failed")
}

internal const val ERROR_CANCELLED = "io.rebble.libpebble3.Error.Cancelled"
internal const val ERROR_TRANSPORT_FAILED = "io.rebble.libpebble3.Error.TransportFailed"
internal const val ERROR_IO = "io.rebble.libpebble3.Error.IO"
