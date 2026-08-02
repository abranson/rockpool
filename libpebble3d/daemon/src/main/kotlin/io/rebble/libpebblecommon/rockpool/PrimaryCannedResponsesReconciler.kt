/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.LibPebbleConfig

internal enum class PrimaryCannedResponsesUpdateResult {
    Completed,
    Cancelled,
    InvalidArgument,
    PersistenceFailed,
    ActivationFailed,
}

private fun LibPebbleConfig.withCannedResponses(responses: List<String>): LibPebbleConfig =
    if (notificationConfig.cannedResponses == responses) {
        this
    } else {
        copy(notificationConfig = notificationConfig.copy(cannedResponses = responses))
    }

/** Replays the canonical primary canned-response collection into libpebble3 config. */
internal class PrimaryCannedResponsesReconciler(
    private val settings: RockpoolSettings,
    private val configMutations: LibPebbleConfigMutationCoordinator,
    private val configFitsStorage: (LibPebbleConfig) -> Boolean,
) {
    private val logger = Logger.withTag("PrimaryCannedResponses")

    @Volatile
    private var reconciliationSettled = false

    /**
     * Whether startup reconciliation no longer needs retrying.
     *
     * This includes an oversized pre-existing canonical record: that corrupt state is preserved
     * for repair, while the last active libpebble3 config remains in force.
     */
    fun isComplete(): Boolean = reconciliationSettled

    /**
     * Commit one canonical replacement and its libpebble3 projection under the config lock.
     *
     * The daemon JVM persists the entire config in one java.util.prefs value. Capacity must be
     * checked against that exact serialized value before the canonical Rockpool record changes;
     * otherwise an accepted collection could become impossible to replay after a restart.
     */
    @Synchronized
    fun replace(
        values: Map<String, String>,
        responses: List<String>,
        beginCommit: () -> Boolean,
    ): PrimaryCannedResponsesUpdateResult {
        var result = PrimaryCannedResponsesUpdateResult.Completed
        try {
            configMutations.mutate { config ->
                val projected = config.withCannedResponses(responses)
                if (!configFitsStorage(projected)) {
                    result = PrimaryCannedResponsesUpdateResult.InvalidArgument
                    return@mutate config
                }
                if (!beginCommit()) {
                    result = PrimaryCannedResponsesUpdateResult.Cancelled
                    return@mutate config
                }
                result = PrimaryCannedResponsesUpdateResult.PersistenceFailed
                val canonical = values + (PRIMARY_CANNED_CONFIGURED_SETTING to "true")
                if (!settings.replacePrefix(PRIMARY_CANNED_PREFIX, canonical)) {
                    return@mutate config
                }
                result = PrimaryCannedResponsesUpdateResult.Completed
                projected
            }
        } catch (e: Throwable) {
            if (result == PrimaryCannedResponsesUpdateResult.Completed) {
                reconciliationSettled = false
                logger.w(e) { "could not activate canonical canned responses; will retry" }
                return PrimaryCannedResponsesUpdateResult.ActivationFailed
            }
            logger.w(e) { "could not replace canonical canned responses" }
            return result
        }
        if (result == PrimaryCannedResponsesUpdateResult.Completed) {
            reconciliationSettled = true
        }
        return result
    }

    @Synchronized
    fun reconcile(): Boolean {
        val canonical = settings.entries(PRIMARY_CANNED_PREFIX)
        val configured = canonical[PRIMARY_CANNED_CONFIGURED_SETTING] == "true" ||
            canonical.keys.any { it.endsWith(PRIMARY_CANNED_NAME_SUFFIX) }
        if (!configured) {
            reconciliationSettled = true
            return true
        }
        val responses = flattenPrimaryCannedResponses(canonical, PRIMARY_CANNED_PREFIX)
        var oversized = false
        reconciliationSettled = runCatching {
            configMutations.mutate { config ->
                val projected = config.withCannedResponses(responses)
                if (!configFitsStorage(projected)) {
                    oversized = true
                    config
                } else {
                    projected
                }
            }
            if (oversized) {
                logger.w {
                    "canonical canned responses exceed config storage capacity; " +
                        "preserving both the canonical record and active config"
                }
            }
            true
        }.getOrElse {
            logger.w(it) { "could not project canonical canned responses; will retry" }
            false
        }
        return reconciliationSettled
    }
}
