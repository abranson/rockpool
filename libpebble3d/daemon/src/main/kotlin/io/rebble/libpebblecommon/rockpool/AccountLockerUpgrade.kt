/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

internal enum class AccountLockerUpgradeResult {
    ALREADY_INITIALIZED,
    INITIALIZED,
    FAILED,
}

/**
 * Retires locker rows whose account ownership predates the account-session gate.
 *
 * A retired marker is persisted inside the same security-ordered transition that clears
 * cloud-owned rows and timeline tokens. The caller promotes it to complete after reconciliation.
 * System applications and local sideloads are retained by libpebble3.
 */
internal suspend fun initializeAccountLockerSessionGate(
    isInitialized: () -> Boolean,
    markRetired: () -> Boolean,
    transitionAccount: suspend (() -> Boolean) -> Boolean,
): AccountLockerUpgradeResult {
    if (isInitialized()) return AccountLockerUpgradeResult.ALREADY_INITIALIZED
    return if (transitionAccount(markRetired)) {
        AccountLockerUpgradeResult.INITIALIZED
    } else {
        AccountLockerUpgradeResult.FAILED
    }
}

internal const val ACCOUNT_LOCKER_SESSION_GATE_MARKER =
    "account.lockerSessionGateInitialized.v1"
internal const val ACCOUNT_LOCKER_SESSION_GATE_RETIRED = "retired"
internal const val ACCOUNT_LOCKER_SESSION_GATE_COMPLETE = "complete"

/** Both states prove that the destructive retirement transition already committed. */
internal fun isAccountLockerSessionGateRetired(marker: String): Boolean =
    marker == ACCOUNT_LOCKER_SESSION_GATE_RETIRED ||
        marker == ACCOUNT_LOCKER_SESSION_GATE_COMPLETE
