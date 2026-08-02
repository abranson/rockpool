/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.util.JvmPaths
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.UUID
import java.util.Properties

/**
 * Small synchronized store for settings that have not yet moved into a
 * libpebble3 database.  It intentionally uses Rockpool naming: the prior
 * rockwork.properties file was unreleased draft state and has no compatibility
 * format.
 */
internal class RockpoolSettings(
    private val file: Path = JvmPaths.dataHome.resolve("rockpool.properties"),
) {
    private val logger = Logger.withTag("RockpoolSettings")
    private val props = Properties()
    private val transientWatchIds = mutableMapOf<String, String>()

    init {
        runCatching {
            if (Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS)) {
                Files.newInputStream(file).use { props.load(it) }
            }
        }.onFailure { logger.w { "failed to load $file: ${it.message}" } }
    }

    @Synchronized
    fun get(key: String, default: String = ""): String = props.getProperty(key) ?: default

    @Synchronized
    fun contains(key: String): Boolean = props.containsKey(key)

    /** A snapshot for typed D-Bus properties; callers must never expose secrets from it. */
    @Synchronized
    fun entries(prefix: String): Map<String, String> = props.stringPropertyNames()
        .asSequence()
        .filter { it.startsWith(prefix) }
        .associateWith { props.getProperty(it).orEmpty() }

    @Synchronized
    fun getBool(key: String, default: Boolean = false): Boolean =
        props.getProperty(key)?.toBooleanStrictOrNull() ?: default

    @Synchronized
    fun getInt(key: String, default: Int): Int = props.getProperty(key)?.toIntOrNull() ?: default

    /**
     * Store one value atomically.  A false return leaves the in-memory view in
     * its previous state as well, so an Operation1 never reports a successful
     * preference update that will disappear after a daemon restart.
     */
    @Synchronized
    fun setChecked(key: String, value: String): Boolean {
        return setAllChecked(mapOf(key to value))
    }

    /** Compatibility convenience for callers that cannot surface I/O errors. */
    fun set(key: String, value: String) {
        setChecked(key, value)
    }

    fun set(key: String, value: Boolean) = set(key, value.toString())
    fun set(key: String, value: Int) = set(key, value.toString())

    /**
     * Store a related group of values in one durable update.  This is used by
     * migration code for records whose value and membership index must not
     * become permanently out of sync after a failed save.
     */
    @Synchronized
    fun setAllChecked(values: Map<String, String>): Boolean {
        if (values.isEmpty() || values.all { (key, value) -> props.getProperty(key) == value }) {
            return true
        }
        val previous = Properties().also { it.putAll(props) }
        values.forEach { (key, value) -> props.setProperty(key, value) }
        if (persistLocked()) return true
        props.clear()
        props.putAll(previous)
        return false
    }

    /**
     * Atomically install a migrated record only when none of its fields already exists.
     * A true result imported it, false preserved current state, and null means persistence failed.
     */
    @Synchronized
    fun setAllIfNoneExistChecked(values: Map<String, String>): Boolean? {
        require(values.isNotEmpty()) { "at least one settings value is required" }
        if (values.keys.any(props::containsKey)) return false
        return if (setAllChecked(values)) true else null
    }

    /**
     * Preserve an existing record value while ensuring its name appears in a
     * delimiter-separated compatibility index. A true result changed and
     * persisted state, false needed no change, and null means the whole
     * related update was rolled back after a failed save.
     */
    @Synchronized
    fun ensureDelimitedListEntryChecked(
        valueKey: String,
        value: String,
        listKey: String,
        entry: String,
        delimiter: String,
    ): Boolean? {
        val entries = props.getProperty(listKey).orEmpty()
            .split(delimiter)
            .filter { it.isNotEmpty() }
        val updates = buildMap {
            if (!props.containsKey(valueKey)) put(valueKey, value)
            if (entry !in entries) put(listKey, (entries + entry).joinToString(delimiter))
        }
        if (updates.isEmpty()) return false
        return if (setAllChecked(updates)) true else null
    }

    /**
     * Replace a complete typed collection below [prefix] in one durable
     * update.  Collection callers cannot leave stale records behind and do
     * not expose a half-written list through D-Bus.
     */
    @Synchronized
    fun replacePrefix(prefix: String, values: Map<String, String>): Boolean {
        return replacePrefixes(mapOf(prefix to values))
    }

    /**
     * Atomically read, transform and replace one complete collection below [prefix].
     * The transform runs while the settings monitor is held, so callers that merge
     * independently-addressed records cannot overwrite a concurrent update from a
     * stale snapshot.
     */
    @Synchronized
    fun updatePrefixChecked(
        prefix: String,
        transform: (Map<String, String>) -> Map<String, String>,
    ): Boolean {
        require(prefix.isNotEmpty()) { "settings prefix must not be empty" }
        val current = props.stringPropertyNames()
            .asSequence()
            .filter { it.startsWith(prefix) }
            .associateWith { props.getProperty(it).orEmpty() }
        val replacement = transform(current)
        require(replacement.keys.all { it.startsWith(prefix) }) {
            "replacement keys must remain below their supplied prefix"
        }
        if (replacement == current) return true

        val previous = Properties().also { it.putAll(props) }
        current.keys.forEach(props::remove)
        replacement.forEach { (key, value) -> props.setProperty(key, value) }
        if (persistLocked()) return true
        props.clear()
        props.putAll(previous)
        return false
    }

    /** Replace multiple disjoint collections in one durable transaction. */
    @Synchronized
    fun replacePrefixes(replacements: Map<String, Map<String, String>>): Boolean {
        require(replacements.isNotEmpty()) { "at least one settings prefix is required" }
        val prefixes = replacements.keys.toList()
        require(prefixes.all(String::isNotEmpty)) { "settings prefix must not be empty" }
        require(prefixes.indices.none { first ->
            prefixes.indices.any { second ->
                first != second && prefixes[first].startsWith(prefixes[second])
            }
        }) {
            "replacement prefixes must not overlap"
        }
        require(replacements.all { (prefix, values) ->
            values.keys.all { it.startsWith(prefix) }
        }) {
            "replacement keys must remain below their supplied prefix"
        }
        val previous = Properties().also { it.putAll(props) }
        props.stringPropertyNames()
            .filter { key -> prefixes.any(key::startsWith) }
            .forEach(props::remove)
        replacements.values.forEach { values ->
            values.forEach { (key, value) -> props.setProperty(key, value) }
        }
        if (persistLocked()) return true
        props.clear()
        props.putAll(previous)
        return false
    }

    /**
     * Return a persistent public watch ID without exposing its address in an
     * object path. A null result means the newly generated or updated mapping
     * could not be made durable, so callers that migrate existing data can
     * defer safely instead of writing beneath a transient identifier.
     */
    @Synchronized
    fun watchObjectIdChecked(serial: String?, address: String): String? =
        watchObjectIdCheckedLocked(serial, address, null)

    /**
     * Normal watch export keeps a process-local fallback while storage is
     * unavailable. If persistence later recovers the same fallback is stored,
     * preventing an object's path from changing during the daemon's lifetime.
     */
    @Synchronized
    fun watchObjectId(serial: String?, address: String): String {
        val normalizedAddress = address.uppercase()
        val fallback = transientWatchIds.getOrPut(normalizedAddress) {
            UUID.randomUUID().toString().replace("-", "")
        }
        val id = watchObjectIdCheckedLocked(serial, normalizedAddress, fallback)
        if (id != null) {
            transientWatchIds.remove(normalizedAddress)
            return id
        }
        logger.w { "watch object ID was not persisted; using a process-local ID" }
        return fallback
    }

    private fun watchObjectIdCheckedLocked(
        serial: String?,
        address: String,
        fallback: String?,
    ): String? {
        val normalizedAddress = address.uppercase()
        val serialKey = serial?.takeIf { it.isNotBlank() }?.let {
            "watch.uuid.serial.${digest(it)}"
        }
        val addressKey = "watch.uuid.address.${digest(normalizedAddress)}"
        val existing = serialKey?.let { props.getProperty(it) }?.takeIf { it.isNotEmpty() }
            ?: props.getProperty(addressKey)?.takeIf { it.isNotEmpty() }
        val id = existing ?: fallback ?: UUID.randomUUID().toString().replace("-", "")
        val values = buildMap {
            if (props.getProperty(addressKey) != id) put(addressKey, id)
            if (serialKey != null && props.getProperty(serialKey) != id) put(serialKey, id)
        }
        return if (setAllChecked(values)) id else null
    }

    @Synchronized
    private fun persistLocked(): Boolean = runCatching {
        Files.createDirectories(file.parent)
        require(!Files.isSymbolicLink(file)) { "settings path must not be a symlink" }
        val temporary = Files.createTempFile(file.parent, ".rockpool-", ".properties")
        try {
            Files.newOutputStream(temporary).use {
                props.store(it, "libpebble3d org.rockpool settings")
            }
            try {
                Files.move(
                    temporary,
                    file,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING,
                )
            } catch (_: AtomicMoveNotSupportedException) {
                Files.move(temporary, file, StandardCopyOption.REPLACE_EXISTING)
            }
        } finally {
            Files.deleteIfExists(temporary)
        }
        true
    }.onFailure { logger.w { "failed to save $file: ${it.message}" } }.getOrDefault(false)

    private fun digest(value: String): String = MessageDigest.getInstance("SHA-256")
        .digest(value.toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }
}
