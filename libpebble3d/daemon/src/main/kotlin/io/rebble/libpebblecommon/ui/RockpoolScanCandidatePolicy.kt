package io.rebble.libpebblecommon.ui

/** Transport-specific identity retained for one legacy mixed-scan result. */
internal enum class RockpoolScanTransport(val dbusName: String) {
    BLE("ble"),
    CLASSIC("classic"),
}

internal data class RockpoolScanCandidate<T>(
    val normalizedAddress: String,
    val device: T,
    val transport: RockpoolScanTransport,
)

/**
 * Pure candidate-selection policy for the legacy mixed BLE/Classic scan.
 *
 * Candidates for hidden addresses are removed, repeated advertisements refresh their own
 * transport, and BLE wins when one address is discovered over both transports.
 */
internal object RockpoolScanCandidatePolicy {
    fun <T> merge(
        existing: Map<String, RockpoolScanCandidate<T>>,
        hiddenAddresses: Set<String>,
        discoveries: Iterable<RockpoolScanCandidate<T>>,
    ): LinkedHashMap<String, RockpoolScanCandidate<T>> {
        val merged = LinkedHashMap(existing)
        merged.keys.removeAll(hiddenAddresses)
        discoveries.forEach { candidate ->
            if (candidate.normalizedAddress in hiddenAddresses) return@forEach
            val previous = merged[candidate.normalizedAddress]
            if (previous == null ||
                previous.transport == candidate.transport ||
                previous.transport == RockpoolScanTransport.CLASSIC &&
                candidate.transport == RockpoolScanTransport.BLE
            ) {
                merged[candidate.normalizedAddress] = candidate
            }
        }
        return merged
    }

    fun <T> matches(
        candidate: RockpoolScanCandidate<T>,
        normalizedAddress: String,
        transport: RockpoolScanTransport?,
    ): Boolean = candidate.normalizedAddress == normalizedAddress &&
        (transport == null || candidate.transport == transport)
}
