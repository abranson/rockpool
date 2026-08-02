package io.rebble.libpebblecommon.compat.rockwork

/** Transport-specific identity retained for one legacy mixed-scan result. */
internal enum class RockworkScanTransport(val dbusName: String) {
    BLE("ble"),
    CLASSIC("classic"),
}

internal data class RockworkScanCandidate<T>(
    val normalizedAddress: String,
    val device: T,
    val transport: RockworkScanTransport,
)

/**
 * Pure candidate-selection policy for the legacy mixed BLE/Classic scan.
 *
 * Candidates for hidden addresses are removed, repeated advertisements refresh their own
 * transport, and BLE wins when one address is discovered over both transports.
 */
internal object RockworkScanCandidatePolicy {
    fun <T> merge(
        existing: Map<String, RockworkScanCandidate<T>>,
        hiddenAddresses: Set<String>,
        discoveries: Iterable<RockworkScanCandidate<T>>,
    ): LinkedHashMap<String, RockworkScanCandidate<T>> {
        val merged = LinkedHashMap(existing)
        merged.keys.removeAll(hiddenAddresses)
        discoveries.forEach { candidate ->
            if (candidate.normalizedAddress in hiddenAddresses) return@forEach
            val previous = merged[candidate.normalizedAddress]
            if (previous == null ||
                previous.transport == candidate.transport ||
                previous.transport == RockworkScanTransport.CLASSIC &&
                candidate.transport == RockworkScanTransport.BLE
            ) {
                merged[candidate.normalizedAddress] = candidate
            }
        }
        return merged
    }

    fun <T> matches(
        candidate: RockworkScanCandidate<T>,
        normalizedAddress: String,
        transport: RockworkScanTransport?,
    ): Boolean = candidate.normalizedAddress == normalizedAddress &&
        (transport == null || candidate.transport == transport)
}
