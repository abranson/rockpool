/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.database.entity.MuteState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList

internal data class NotificationFilterMutation(
    val activePinsUpdated: Boolean,
    val filter: PlatformNotificationFilter?,
    val runtimePolicyUpdated: Boolean,
    val applicationStateUpdated: Boolean,
)

/**
 * Serializes notification policy changes made through the primary and compatibility APIs.
 *
 * Provider filtering happens before libpebble3 learns a notification application.  The provider
 * policy must therefore become permissive before a forgotten database row is retired, otherwise
 * the next post can be suppressed forever instead of learning the application again.
 */
internal class NotificationFilterCoordinator(
    private val loadEntries: (String) -> Map<String, String>,
    private val replacePrefixes: (Map<String, Map<String, String>>) -> Boolean,
    private val replaceRuntimeFilters: suspend (
        Map<String, PlatformNotificationFilter>,
    ) -> Boolean,
    private val updateMuteState: suspend (String, MuteState) -> Unit,
    private val forgetApplication: suspend (String) -> Unit,
    private val reconcileMuteStates: suspend (Map<String, MuteState>) -> Unit,
    private val muteStateTimeoutMs: Long = MUTE_STATE_TIMEOUT_MS,
) {
    private val logger = Logger.withTag("NotificationFilters")
    private val mutex = Mutex()
    private val listeners = CopyOnWriteArrayList<() -> Unit>()
    @Volatile
    private var reconciliationNeeded = true

    init {
        require(muteStateTimeoutMs > 0)
    }

    fun addListener(listener: () -> Unit) {
        listeners += listener
    }

    fun needsReconciliation(): Boolean = reconciliationNeeded

    /**
     * Replay persisted policy into both runtime enforcement layers after legacy import.  The
     * complete learned-application reconciliation also resets rows for sources omitted from the
     * canonical map, repairing a crash between an earlier policy commit and its Room update.
     */
    suspend fun reconcilePersistedState(): Boolean = mutex.withLock {
        try {
            withContext(NonCancellable) {
                val configured = currentFilters()
                // The provider must install restrictive/conditional policy before an existing
                // database row can become permissive.
                replaceRuntimeFilters(configured)
                val reconciled = withTimeoutOrNull(muteStateTimeoutMs) {
                    reconcileMuteStates(muteStates(configured))
                    true
                } == true
                if (!reconciled) {
                    reconciliationNeeded = true
                    logger.w { "notification application reconciliation timed out" }
                    return@withContext false
                }
                val retirements = currentRetirements()
                val retired = withTimeoutOrNull(muteStateTimeoutMs) {
                    retirements.toSortedSet().forEach { source -> forgetApplication(source) }
                    clearRetirements(retirements)
                } == true
                if (!retired) {
                    reconciliationNeeded = true
                    logger.w { "notification application retirement timed out or was not persisted" }
                    return@withContext false
                }
                reconciliationNeeded = false
                true
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            reconciliationNeeded = true
            logger.w(e) { "notification filter reconciliation failed" }
            false
        }
    }

    /** Replace the account-global policy from a primary per-watch API object. */
    suspend fun replacePrimary(
        watchPrefix: String,
        watchValues: Map<String, String>,
        configured: Map<String, PlatformNotificationFilter>,
    ): NotificationFilterMutation? = mutex.withLock {
        require(watchPrefix.startsWith("watch.") && watchPrefix.endsWith(".notifications."))
        require(watchValues.keys.all { it.startsWith(watchPrefix) })

        // Notifications1 intentionally exposes a binary enabled property, whereas the active
        // compatibility UI also owns mode 1 (enabled except while the phone is active). An
        // unchanged primary round-trip must not silently widen that policy to always enabled.
        val previous = currentFilters()
        val effective = configured.mapValues { (source, requested) ->
            val existing = previous[source]
            if (requested.enabled && existing?.suppressWhenActive == true) {
                requested.copy(suppressWhenActive = true)
            } else {
                requested
            }
        }
        val replacements = emptyLegacyWatchFilters().toMutableMap().apply {
            put(watchPrefix, encodeNotificationFilters(watchPrefix, effective))
            put(
                GLOBAL_NOTIFICATION_FILTER_PREFIX,
                encodeCanonicalNotificationFilters(effective),
            )
            put(
                GLOBAL_NOTIFICATION_RETIREMENT_PREFIX,
                encodeNotificationRetirements(currentRetirements() - effective.keys),
            )
        }
        commit(replacements, previous, effective)
    }

    /** Apply the legacy 0/1/2 filter state, preserving conditional MCE-active suppression. */
    suspend fun setCompatibilityFilter(
        source: String,
        enabled: Int,
        applicationName: String = source,
    ): NotificationFilterMutation? = mutex.withLock {
        if (source.isBlank() || enabled !in NOTIFICATION_DISABLED..NOTIFICATION_ENABLED) {
            return@withLock null
        }
        val previous = currentFilters()
        val old = previous[source]
        val filter = PlatformNotificationFilter.fromMode(
            enabled,
            old?.name ?: applicationName.ifBlank { source },
            old?.icon.orEmpty(),
        )
        val configured = previous + (source to filter)
        val replacements = emptyLegacyWatchFilters().toMutableMap().apply {
            put(
                GLOBAL_NOTIFICATION_FILTER_PREFIX,
                encodeCanonicalNotificationFilters(configured),
            )
            put(
                GLOBAL_NOTIFICATION_RETIREMENT_PREFIX,
                encodeNotificationRetirements(currentRetirements() - source),
            )
        }
        commit(replacements, previous, configured)?.copy(filter = filter)
    }

    /** Remove policy and application state so the next permitted post learns it afresh. */
    suspend fun forgetCompatibilityFilter(source: String): NotificationFilterMutation? =
        mutex.withLock {
            if (source.isBlank()) return@withLock null
            val previous = currentFilters()
            val configured = previous - source
            val replacements = emptyLegacyWatchFilters().toMutableMap().apply {
                put(
                    GLOBAL_NOTIFICATION_FILTER_PREFIX,
                    encodeCanonicalNotificationFilters(configured),
                )
                put(
                    GLOBAL_NOTIFICATION_RETIREMENT_PREFIX,
                    encodeNotificationRetirements(currentRetirements() + source),
                )
            }
            commit(
                replacements = replacements,
                previous = previous,
                configured = configured,
                afterRuntime = {
                    forgetApplication(source)
                    check(clearRetirements(setOf(source))) {
                        "notification retirement intent was not persisted"
                    }
                },
            )
        }

    private suspend fun commit(
        replacements: Map<String, Map<String, String>>,
        previous: Map<String, PlatformNotificationFilter>,
        configured: Map<String, PlatformNotificationFilter>,
        afterRuntime: suspend () -> Unit = {},
    ): NotificationFilterMutation? {
        if (!replacePrefixes(replacements)) return null
        return withContext(NonCancellable) {
            val affectedSources = previous.keys + configured.keys
            val desiredMuteStates = muteStates(configured, affectedSources)
            var runtimePolicyUpdated = true
            val activePinsUpdated = try {
                replaceRuntimeFilters(configured)
            } catch (e: Exception) {
                runtimePolicyUpdated = false
                reconciliationNeeded = true
                logger.w(e) { "notification provider policy handoff failed" }
                false
            }
            val muteStateUpdated = applyMuteStates(desiredMuteStates)
            val afterRuntimeUpdated = try {
                val updated = withTimeoutOrNull(muteStateTimeoutMs) {
                    afterRuntime()
                    true
                } == true
                if (!updated) {
                    reconciliationNeeded = true
                    logger.w { "notification application retirement timed out" }
                }
                updated
            } catch (e: Exception) {
                reconciliationNeeded = true
                logger.w(e) { "notification application retirement failed" }
                false
            }
            notifyListeners()
            NotificationFilterMutation(
                activePinsUpdated = activePinsUpdated,
                filter = null,
                runtimePolicyUpdated = runtimePolicyUpdated,
                applicationStateUpdated = muteStateUpdated && afterRuntimeUpdated,
            )
        }
    }

    private suspend fun applyMuteStates(states: Map<String, MuteState>): Boolean {
        return try {
            val updated = withTimeoutOrNull(muteStateTimeoutMs) {
                states.toSortedMap().forEach { (source, state) -> updateMuteState(source, state) }
                true
            } == true
            if (!updated) {
                reconciliationNeeded = true
                logger.w { "notification application mute update timed out" }
            }
            updated
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            reconciliationNeeded = true
            logger.w(e) { "notification application mute update failed" }
            false
        }
    }

    private fun muteStates(
        filters: Map<String, PlatformNotificationFilter>,
        sources: Set<String> = filters.keys,
    ): Map<String, MuteState> = sources.associateWith { source ->
        if (filters[source]?.enabled == false) MuteState.Always else MuteState.Never
    }

    private fun currentFilters(): Map<String, PlatformNotificationFilter> {
        val canonical = loadEntries(GLOBAL_NOTIFICATION_FILTER_PREFIX)
        if (hasCanonicalNotificationFilters(canonical)) {
            return notificationFiltersFromEntries(canonical)
        }
        return notificationFiltersFromEntries(loadEntries("watch."))
    }

    private fun currentRetirements(): Set<String> =
        notificationRetirementsFromEntries(loadEntries(GLOBAL_NOTIFICATION_RETIREMENT_PREFIX))

    private fun clearRetirements(sources: Set<String>): Boolean {
        if (sources.isEmpty()) return true
        val current = currentRetirements()
        val remaining = current - sources
        if (remaining == current) return true
        return replacePrefixes(
            mapOf(
                GLOBAL_NOTIFICATION_RETIREMENT_PREFIX to
                    encodeNotificationRetirements(remaining),
            )
        )
    }

    /**
     * Once either API writes canonical state, retire every migration fallback atomically.  This
     * is essential for an empty canonical set: the loader otherwise has no source row with which
     * to distinguish "authoritatively empty" from "not migrated yet".
     */
    private fun emptyLegacyWatchFilters(): Map<String, Map<String, String>> =
        notificationWatchPrefixes(loadEntries("watch.")).associateWith { emptyMap() }

    private fun notifyListeners() {
        listeners.forEach { listener ->
            runCatching(listener).onFailure {
                logger.w { "notification filter listener failed: ${it.message}" }
            }
        }
    }

    private companion object {
        private const val MUTE_STATE_TIMEOUT_MS = 5_000L
    }
}

internal fun notificationWatchPrefixes(values: Map<String, String>): Set<String> = values.keys
    .mapNotNull { key ->
        val marker = ".notifications."
        val markerIndex = key.indexOf(marker)
        if (!key.startsWith("watch.") || markerIndex < 0) null
        else key.substring(0, markerIndex + marker.length)
    }
    .toSet()

internal fun encodeNotificationFilters(
    prefix: String,
    filters: Map<String, PlatformNotificationFilter>,
): Map<String, String> = buildMap {
    filters.toSortedMap().forEach { (source, filter) ->
        val id = UUID.nameUUIDFromBytes(source.toByteArray(Charsets.UTF_8))
            .toString().replace("-", "")
        val itemPrefix = "$prefix$id."
        put("${itemPrefix}source", source)
        put("${itemPrefix}enabled", filter.mode.toString())
        put("${itemPrefix}name", filter.name)
        put("${itemPrefix}icon", filter.icon)
    }
}

internal fun encodeCanonicalNotificationFilters(
    filters: Map<String, PlatformNotificationFilter>,
): Map<String, String> = encodeNotificationFilters(GLOBAL_NOTIFICATION_FILTER_PREFIX, filters) +
    (GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING to "true")

internal const val GLOBAL_NOTIFICATION_RETIREMENT_PREFIX = "notificationRetirements."

internal fun notificationRetirementsFromEntries(values: Map<String, String>): Set<String> =
    values.entries
        .asSequence()
        .filter { (key, value) -> key.endsWith(".source") && value.isNotBlank() }
        .map(Map.Entry<String, String>::value)
        .toSet()

internal fun encodeNotificationRetirements(sources: Set<String>): Map<String, String> = buildMap {
    sources.toSortedSet().forEach { source ->
        val id = UUID.nameUUIDFromBytes(source.toByteArray(Charsets.UTF_8))
            .toString().replace("-", "")
        put("$GLOBAL_NOTIFICATION_RETIREMENT_PREFIX$id.source", source)
    }
}
