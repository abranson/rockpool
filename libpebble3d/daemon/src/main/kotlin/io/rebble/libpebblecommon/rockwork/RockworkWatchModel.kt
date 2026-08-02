/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.metadata.WatchColor

/**
 * The connected protocol result is authoritative, while known watches retain the last
 * negotiated colour for compatibility clients while they are disconnected.
 */
internal fun rockworkWatchModel(
    connectedColor: WatchColor?,
    knownColor: WatchColor?,
): Int = (connectedColor ?: knownColor)?.protocolNumber ?: 0
