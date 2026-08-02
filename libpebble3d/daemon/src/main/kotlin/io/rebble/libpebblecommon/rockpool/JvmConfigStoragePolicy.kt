/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.LibPebbleConfig
import java.util.prefs.Preferences

/** Capacity policy for libpebble3's single-value JVM Preferences config store. */
internal class JvmConfigStoragePolicy(
    private val serializedLength: (LibPebbleConfig) -> Int,
    private val maximumLength: Int = Preferences.MAX_VALUE_LENGTH,
) {
    fun canPersist(config: LibPebbleConfig): Boolean = serializedLength(config) <= maximumLength

    fun withMandatoryDaemonPolicy(config: LibPebbleConfig): LibPebbleConfig? {
        val updated = config.copy(
            watchConfig = config.watchConfig.copy(
                multipleConnectedWatchesSupported = true,
                lanDevConnection = true,
            ),
        )
        return updated.takeIf(::canPersist)
    }

    /**
     * Admit canned responses only if every config variant the daemon can later persist still fits.
     *
     * Calendar enablement is mutable through the compatibility API and PPoG verbosity can change
     * between daemon starts. The multi-watch and local developer-transport flags are mandatory
     * daemon policy, so include their enabled representation even if a test candidate omits them.
     */
    fun canAdmitCannedResponses(config: LibPebbleConfig): Boolean =
        listOf(false, true).all { calendarEnabled ->
            listOf(false, true).all { verbosePpog ->
                withMandatoryDaemonPolicy(
                    config.copy(
                        bleConfig = config.bleConfig.copy(verbosePpogLogging = verbosePpog),
                        watchConfig = config.watchConfig.copy(
                            calendarPins = calendarEnabled,
                        ),
                    ),
                ) != null
            }
        }
}

/** Applies mandatory daemon config without letting an old near-capacity record abort startup. */
internal fun applyMandatoryDaemonConfigPolicy(
    configMutations: LibPebbleConfigMutationCoordinator,
    storagePolicy: JvmConfigStoragePolicy,
): Boolean {
    var accepted = true
    configMutations.mutate { current ->
        storagePolicy.withMandatoryDaemonPolicy(current) ?: run {
            accepted = false
            current
        }
    }
    return accepted
}
