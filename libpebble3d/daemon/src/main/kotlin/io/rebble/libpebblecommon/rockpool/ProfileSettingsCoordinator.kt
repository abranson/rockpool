/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import java.util.concurrent.CopyOnWriteArrayList

internal data class ProfileSettingsChange(
    val address: String,
    val kinds: Set<String>,
)

/** Keeps the primary and temporary compatibility profile settings as one atomic record. */
internal class ProfileSettingsCoordinator(
    private val objectIdForWatch: (String?, String) -> String?,
    private val persist: (Map<String, String>) -> Boolean,
) {
    constructor(settings: RockpoolSettings) : this(
        objectIdForWatch = settings::watchObjectIdChecked,
        persist = settings::setAllChecked,
    )

    private val listeners = CopyOnWriteArrayList<(ProfileSettingsChange) -> Unit>()

    fun updateByObjectId(
        objectId: String,
        address: String,
        profiles: Map<String, String>,
    ): Boolean = update(objectId, address, profiles)

    fun updateForWatch(
        serial: String?,
        address: String,
        profiles: Map<String, String>,
    ): Boolean {
        val objectId = objectIdForWatch(serial, address) ?: return false
        return update(objectId, address, profiles)
    }

    fun addListener(listener: (ProfileSettingsChange) -> Unit): AutoCloseable {
        listeners += listener
        return AutoCloseable { listeners -= listener }
    }

    private fun update(
        objectId: String,
        address: String,
        profiles: Map<String, String>,
    ): Boolean {
        require(profiles.isNotEmpty()) { "at least one profile setting is required" }
        require(profiles.keys.all { it == "connected" || it == "disconnected" }) {
            "unknown profile setting"
        }
        val compatibilityPrefix = "${address.uppercase().replace(":", "_")}.profile."
        val primaryPrefix = "watch.$objectId.profiles."
        val values = buildMap {
            profiles.forEach { (kind, profile) ->
                put("$primaryPrefix$kind", profile)
                put("$compatibilityPrefix$kind", profile)
            }
        }
        if (!persist(values)) return false
        val change = ProfileSettingsChange(address.uppercase(), profiles.keys.toSet())
        listeners.forEach { it(change) }
        return true
    }
}
