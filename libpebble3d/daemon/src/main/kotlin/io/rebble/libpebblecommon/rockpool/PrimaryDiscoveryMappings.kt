/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

internal enum class PrimaryDiscoveryTransport(val value: String) {
    BLE("ble"),
    CLASSIC("classic"),
}

internal fun primaryScanning(ble: Boolean, classic: Boolean): Boolean = ble || classic

/** Cross Operation1's commit boundary immediately before mutating scan state. */
internal inline fun runCommittedScanMutation(
    beginCommit: () -> Boolean,
    mutate: () -> Unit,
): Boolean {
    if (!beginCommit()) return false
    mutate()
    return true
}

internal fun primaryManagerCapabilities(
    classicAvailable: Boolean,
    concurrentWatches: Boolean,
): List<String> = buildList {
    add("transport.ble")
    add("discovery.bond-import")
    if (classicAvailable) add("transport.classic")
    if (concurrentWatches) add("watch.concurrent")
}

internal fun primaryScanTransport(
    requested: String?,
    classicAvailable: Boolean,
): PrimaryDiscoveryTransport? = when (requested ?: "any") {
    "any", "ble" -> PrimaryDiscoveryTransport.BLE
    "classic" -> PrimaryDiscoveryTransport.CLASSIC.takeIf { classicAvailable }
    else -> null
}

internal fun primaryPairTransportSupported(
    requested: String,
    classicAvailable: Boolean,
): Boolean = when (requested) {
    "any", "ble" -> true
    "classic" -> classicAvailable
    else -> false
}

internal fun primaryPairTransportMatches(
    requested: String,
    actual: String,
    classicAvailable: Boolean,
): Boolean {
    if (actual == "classic" && !classicAvailable) return false
    return requested == "any" || requested == actual
}
