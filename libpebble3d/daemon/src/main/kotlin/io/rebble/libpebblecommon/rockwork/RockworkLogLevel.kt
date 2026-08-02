/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import co.touchlab.kermit.Severity

/** Compatibility indices; the current UI toggles debug messages on over normal Info logging. */
internal fun rockworkLogSeverity(level: Int): Severity = when {
    level <= 0 -> Severity.Debug
    level == 1 -> Severity.Info
    else -> Severity.Error
}

internal fun normalizedRockworkLogLevel(level: Int): Int = level.coerceIn(0, 2)
