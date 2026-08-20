/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.compat.rockwork.ROCKWORK_WEATHER_SETTINGS_PREFIX
import io.rebble.libpebblecommon.compat.rockwork.ROCKWORK_MAX_WEATHER_LOCATIONS
import io.rebble.libpebblecommon.compat.rockwork.RockworkWeatherLocation
import io.rebble.libpebblecommon.compat.rockwork.encodeRockworkWeatherSettings
import io.rebble.libpebblecommon.compat.rockwork.parseRockworkWeatherLocations
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.bt.ble.bluez.BluezManager
import io.rebble.libpebblecommon.connection.endpointmanager.blobdb.normalizedTimelineWindow
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.freedesktop.dbus.types.Variant
import java.io.IOException
import java.net.URLDecoder
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.security.MessageDigest
import kotlin.io.path.name
import kotlin.time.Duration.Companion.seconds

/**
 * Conservative, versioned migration from the old C++ daemon's QSettings tree.
 * It never modifies the legacy directory and writes a value only when the new
 * store has no value for it.  Unmatched watch directories keep the importer
 * pending so users upgrading before a BlueZ/libpebble3 import are not skipped.
 */
internal class LegacyRockpooldImporter(
    private val settings: RockpoolSettings,
    private val legacyRoot: Path? = null,
    private val readBondedAddresses: suspend () -> Set<String> = { BluezManager.bondedAddresses() },
) {
    private val logger = Logger.withTag("LegacyRockpooldImporter")
    private val importLock = Mutex()
    private var writeFailed = false

    suspend fun importIfNeeded(libPebble: LibPebble) {
        importLock.withLock {
            importLocked(libPebble)
        }
    }

    fun isOriginalImportComplete(): Boolean = settings.get(MARKER) == COMPLETE

    fun isWeatherMigrationComplete(): Boolean = settings.get(WEATHER_MARKER) == COMPLETE

    fun isComplete(): Boolean = isOriginalImportComplete() && isWeatherMigrationComplete()

    private suspend fun importLocked(libPebble: LibPebble) {
        if (isComplete()) return
        writeFailed = false
        try {
            if (
                settings.get(WEATHER_MARKER) != COMPLETE &&
                settings.entries(ROCKWORK_WEATHER_SETTINGS_PREFIX).isNotEmpty()
            ) {
                completeWeatherMigration(PRESERVED_CURRENT)
                if (isComplete()) return
            }
            val root = legacyRoot ?: run {
                val home = System.getProperty("user.home")?.takeIf { it.isNotBlank() } ?: return
                Path.of(home, ".local", "share", "rockpoold")
            }
            if (!Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS)) {
                if (settings.get(MARKER) != COMPLETE && !settings.setChecked(MARKER, COMPLETE)) {
                    writeFailed = true
                    logger.w { "legacy migration marker was not persisted; will retry" }
                }
                if (settings.get(WEATHER_MARKER) != COMPLETE) {
                    completeWeatherMigration(NO_SOURCE)
                }
                return
            }

            val knownByAddress = libPebble.watches.value.filterIsInstance<KnownPebbleDevice>()
                .associateBy { it.identifier.asString.uppercase() }
            val bondedAddresses = withTimeoutOrNull(5.seconds) {
                readBondedAddresses()
            }.orEmpty().mapTo(mutableSetOf()) { it.uppercase() }
            val v1Pending = settings.get(MARKER) != COMPLETE
            val legacyDirectories = Files.newDirectoryStream(root).use { directories ->
                directories.asSequence()
                    .filter { Files.isDirectory(it, LinkOption.NOFOLLOW_LINKS) }
                    .mapNotNull { directory ->
                        addressFromLegacyDirectory(directory.name)?.let { directory to it }
                    }
                    .sortedBy { it.second }
                    .toList()
            }
            var imported = 0
            var pending = false
            val eligibleDirectories = mutableListOf<Path>()
            if (v1Pending) {
                val accountImported = importAccount(
                    root = root,
                    eligibleAddresses = knownByAddress.keys + bondedAddresses,
                )
                if (accountImported) imported++
            }

            legacyDirectories.forEach { (directory, address) ->
                val known = knownByAddress[address]
                if (known == null && address !in bondedAddresses) {
                    pending = true
                    return@forEach
                }
                eligibleDirectories.add(directory)
                if (!v1Pending) return@forEach
                val watchId = settings.watchObjectIdChecked(known?.serial, address)
                if (watchId == null) {
                    writeFailed = true
                    pending = true
                    return@forEach
                }
                imported += importWatch(directory, watchId, address)
            }

            var v1Complete = !v1Pending
            if (v1Pending && !pending && !writeFailed) {
                v1Complete = settings.setChecked(MARKER, COMPLETE)
                if (!v1Complete) writeFailed = true
            }
            if (
                v1Complete && !pending && !writeFailed &&
                settings.get(WEATHER_MARKER) != COMPLETE
            ) {
                importWeatherLocations(eligibleDirectories)?.let(::completeWeatherMigration)
            }
            if (isComplete()) {
                logger.i { "legacy state migration complete; $imported values imported" }
            } else {
                logger.i { "legacy state migration deferred; $imported values imported" }
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logger.w { "legacy state migration could not read the source; will retry" }
        }
    }

    /**
     * Consolidates the old per-watch QSettings arrays into one account-global collection only
     * when every present, valid collection is identical. Existing canonical state always wins.
     */
    private fun importWeatherLocations(directories: List<Path>): String? {
        if (settings.entries(ROCKWORK_WEATHER_SETTINGS_PREFIX).isNotEmpty()) return PRESERVED_CURRENT
        val candidates = mutableListOf<List<RockworkWeatherLocation>>()
        var invalid = false
        directories.forEach { directory ->
            val values = readIni(directory.resolve("appsettings.conf"))
            if (writeFailed) return null
            if ("weatherApp/size" !in values) return@forEach
            decodeLegacyWeatherLocations(values)
                .onSuccess { candidates.add(it) }
                .onFailure { invalid = true }
        }
        if (invalid) return INVALID_SOURCE
        if (candidates.isEmpty()) return NO_SOURCE
        val distinct = candidates.distinct()
        if (distinct.size != 1) return CONFLICT
        val encoded = encodeRockworkWeatherSettings(distinct.single())
        var preservedCurrent = false
        val persisted = settings.updatePrefixChecked(ROCKWORK_WEATHER_SETTINGS_PREFIX) { current ->
            if (current.isEmpty()) {
                encoded
            } else {
                preservedCurrent = true
                current
            }
        }
        if (!persisted) {
            writeFailed = true
            return null
        }
        if (preservedCurrent) return PRESERVED_CURRENT
        return if (candidates.size == 1) IMPORTED_SINGLE else IMPORTED_IDENTICAL
    }

    private fun decodeLegacyWeatherLocations(
        values: Map<String, String>,
    ): Result<List<RockworkWeatherLocation>> = runCatching {
        val count = requireNotNull(values["weatherApp/size"]?.toIntOrNull())
        require(count in 0..ROCKWORK_MAX_WEATHER_LOCATIONS)
        val variants = (1..count).map { index ->
            Variant(
                listOf(
                    requireNotNull(values["weatherApp/$index/name"]),
                    requireNotNull(values["weatherApp/$index/lat"]),
                    requireNotNull(values["weatherApp/$index/lng"]),
                ),
                "as",
            )
        }
        parseRockworkWeatherLocations(variants)
    }

    private fun completeWeatherMigration(outcome: String): Boolean = settings.setAllChecked(
        mapOf(
            WEATHER_OUTCOME to outcome,
            WEATHER_MARKER to COMPLETE,
        ),
    ).also { persisted ->
        if (!persisted) {
            writeFailed = true
            logger.w { "legacy weather-location migration marker was not persisted; will retry" }
        }
    }

    private fun importAccount(root: Path, eligibleAddresses: Set<String>): Boolean {
        if (settings.contains("account.oauthToken")) return false
        val candidates = Files.newDirectoryStream(root).use { directories ->
            val values = linkedSetOf<String>()
            for (directory in directories) {
                if (values.size > 1) break
                if (!Files.isDirectory(directory, LinkOption.NOFOLLOW_LINKS)) continue
                val address = addressFromLegacyDirectory(directory.name) ?: continue
                if (address !in eligibleAddresses) continue
                val token = readIniChecked(directory.resolve("timeline/sync.ini"))["oauthToken"]
                    ?.takeIf { it.isNotBlank() }
                    ?: continue
                values += token
            }
            values
        }
        if (candidates.size > 1) {
            // The replacement has one account-global identity. Never pick one
            // old per-watch credential by filesystem iteration order.
            logger.w { "legacy account credentials conflict; explicit sign-in is required" }
            return false
        }
        val token = candidates.singleOrNull() ?: return false
        // Never log or return the token.  It remains write-only through Account1.
        return settings.setChecked("account.oauthToken", token).also { persisted ->
            if (!persisted) writeFailed = true
        }
    }

    private fun importWatch(directory: Path, watchId: String, address: String): Int {
        var count = 0
        val prefix = "watch.$watchId"
        val compatPrefix = address.replace(':', '_')
        val appSettings = readIni(directory.resolve("appsettings.conf"))
        val healthKeys = listOf(
            "enabled",
            "age",
            "height",
            "gender",
            "weight",
            "moreActive",
            "sleepMore",
        )
        healthKeys.forEach { key ->
            count += copyWatchAndCompatibility(
                "$prefix.health.$key",
                "health.$key",
                appSettings["activityParams/$key"],
            )
        }
        count += copyWatchAndCompatibility(
            "$prefix.units.imperial",
            "imperialUnits",
            appSettings["unitsDistance/imperialUnits"] ?: appSettings["unitsDistance/enabled"],
        )
        count += copyIfMissing("$prefix.calendar.enabled", appSettings["calendar/calendarSyncEnabled"])
        count += copyWatchAndCompatibility(
            "$prefix.profiles.connected",
            "$compatPrefix.profile.connected",
            appSettings["profileWhen/connected"],
        )
        count += copyWatchAndCompatibility(
            "$prefix.profiles.disconnected",
            "$compatPrefix.profile.disconnected",
            appSettings["profileWhen/disconnected"],
        )
        count += importTimelineWindow(compatPrefix, appSettings)
        // Only supported weather preferences migrate.  Legacy provider keys are credentials for
        // dead WU/TWC endpoints and are intentionally excluded.
        count += copyWatchAndCompatibility(
            "$prefix.weather.units",
            "weather.units",
            appSettings["weatherApp/units"],
        )
        count += copyWatchAndCompatibility(
            "$prefix.weather.language",
            "weather.language",
            appSettings["weatherApp/language"],
        )

        count += importNotificationFilters(prefix, readIni(directory.resolve("notifications.conf")))
        count += importCanned(prefix, readIni(directory.resolve("canned_messages.conf")))
        return count
    }

    /**
     * Timeline preferences are one record: importing individual fields could
     * permanently create a window the old daemon never represented. Preserve
     * any current group rather than merging a legacy partial over it.
     */
    private fun importTimelineWindow(compatPrefix: String, values: Map<String, String>): Int {
        val prefix = "$compatPrefix.timeline"
        val window = normalizedTimelineWindow(
            values["timeline/pastDays"]?.toIntOrNull() ?: return 0,
            values["timeline/eventFadeout"]?.toIntOrNull() ?: return 0,
            values["timeline/futureDays"]?.toIntOrNull() ?: return 0,
        ) ?: return 0
        val imported = mapOf(
            "$prefix.start" to window.pastDays.toString(),
            "$prefix.fade" to window.notificationFadeSeconds.toString(),
            "$prefix.end" to window.futureDays.toString(),
        )
        return when (settings.setAllIfNoneExistChecked(imported)) {
            true -> imported.size
            false -> 0
            null -> {
                writeFailed = true
                0
            }
        }
    }

    private fun importNotificationFilters(prefix: String, values: Map<String, String>): Int {
        // A marker-only canonical collection is an explicit empty policy. Never recreate the
        // per-watch migration fallback after either API has committed authoritative state.
        if (settings.get(GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING) == "true") return 0
        var count = 0
        val grouped = values.entries.groupBy {
            qtPath(it.key).substringBeforeLast('/', missingDelimiterValue = "")
        }
        grouped.forEach { (source, entries) ->
            if (source.isBlank()) return@forEach
            val id = digest(source)
            count += copyIfMissing("$prefix.notifications.$id.source", source)
            entries.forEach { entry ->
                when (qtPath(entry.key).substringAfterLast('/')) {
                    "enabled", "name", "icon" -> {
                        count += copyIfMissing(
                            "$prefix.notifications.$id.${qtPath(entry.key).substringAfterLast('/')}",
                            entry.value,
                        )
                    }
                }
            }
        }
        return count
    }

    private fun importCanned(prefix: String, values: Map<String, String>): Int {
        var count = 0
        count += importArrays(prefix, "canned", "canned", "msg", values)
        count += importArrays(prefix, "favourites", "contacts", "ctx", values)
        return count
    }

    private fun importArrays(
        prefix: String,
        watchKind: String,
        compatibilityKind: String,
        field: String,
        values: Map<String, String>,
    ): Int {
        var count = 0
        val groups = linkedMapOf<String, MutableList<Pair<Int, String>>>()
        values.forEach { (key, value) ->
            val normalized = qtPath(key)
            val suffix = "/$field"
            if (!normalized.endsWith(suffix)) return@forEach
            val beforeField = normalized.removeSuffix(suffix)
            val indexStart = beforeField.lastIndexOf('/')
            if (indexStart <= 0) return@forEach
            val index = beforeField.substring(indexStart + 1).toIntOrNull() ?: return@forEach
            val group = beforeField.substring(0, indexStart)
            if (group.isBlank()) return@forEach
            groups.getOrPut(group) { mutableListOf() } += index to value
        }
        groups.forEach { (group, entries) ->
            val id = digest(group)
            count += copyIfMissing("$prefix.$watchKind.$id.name", group)
            count += copyIfMissing(
                "$prefix.$watchKind.$id.values",
                entries.sortedBy { it.first }.joinToString(SEPARATOR) { it.second },
            )
            count += copyCompatibilityList(
                compatibilityKind,
                group,
                entries.sortedBy { it.first }.map { it.second },
            )
        }
        return count
    }

    private fun copyWatchAndCompatibility(
        watchKey: String,
        compatibilityKey: String,
        value: String?,
    ): Int = copyIfMissing(watchKey, value) + copyIfMissing(compatibilityKey, value)

    /** Preserve any values set by the current compatibility UI or libpebble3. */
    private fun copyCompatibilityList(kind: String, name: String, values: List<String>): Int {
        if (values.isEmpty()) return 0
        return when (
            settings.ensureDelimitedListEntryChecked(
                valueKey = "$kind.$name",
                value = values.joinToString(SEPARATOR),
                listKey = "$kind.keys",
                entry = name,
                delimiter = SEPARATOR,
            )
        ) {
            true -> 1
            false -> 0
            null -> {
                writeFailed = true
                0
            }
        }
    }

    private fun copyIfMissing(key: String, value: String?): Int {
        if (value.isNullOrBlank() || settings.contains(key)) return 0
        if (!settings.setChecked(key, value)) {
            writeFailed = true
            return 0
        }
        return 1
    }

    /** Read just the QSettings INI subset used by the migration allowlist. */
    private fun readIni(path: Path): Map<String, String> {
        return runCatching {
            readIniChecked(path)
        }.getOrElse {
            writeFailed = true
            logger.w { "could not read legacy settings ${path.fileName}" }
            emptyMap()
        }
    }

    /** Missing sources are benign; existing unusable sources keep the migration retryable. */
    private fun readIniChecked(path: Path): Map<String, String> {
        if (Files.notExists(path, LinkOption.NOFOLLOW_LINKS)) return emptyMap()
        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) {
            throw IOException("legacy settings source is not a regular file")
        }
        val values = linkedMapOf<String, String>()
        var section = ""
        Files.newBufferedReader(path, StandardCharsets.UTF_8).useLines { lines ->
            lines.forEach { raw ->
                val line = raw.trim()
                when {
                    line.isEmpty() || line.startsWith(';') || line.startsWith('#') -> Unit
                    line.startsWith('[') && line.endsWith(']') -> {
                        section = decode(line.substring(1, line.length - 1))
                    }
                    else -> {
                        val separator = line.indexOf('=')
                        if (separator <= 0) return@forEach
                        val key = decode(line.substring(0, separator))
                        val value = decode(line.substring(separator + 1))
                        val fullKey = if (section.isBlank() || section == "%General") {
                            key
                        } else {
                            "$section/$key"
                        }
                        values[qtPath(fullKey)] = value
                    }
                }
            }
        }
        return values
    }

    private fun decode(value: String): String = runCatching {
        // Qt's INI backend percent-encodes non-ASCII key/value bytes.  Keep '+' literal.
        URLDecoder.decode(value.replace("+", "%2B"), StandardCharsets.UTF_8)
    }.getOrDefault(value)

    /** Qt's INI backend writes array/group separators as backslashes on some releases. */
    private fun qtPath(value: String): String = value.replace('\\', '/')

    private fun addressFromLegacyDirectory(name: String): String? {
        if (!LEGACY_ADDRESS.matches(name)) return null
        return name.replace('_', ':').uppercase()
    }

    private fun digest(value: String): String = MessageDigest.getInstance("SHA-256")
        .digest(value.toByteArray(StandardCharsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }

    companion object {
        private const val MARKER = "migration.rockpoold.v1"
        private const val WEATHER_MARKER = "migration.rockpoold.weather-locations.v1"
        private const val WEATHER_OUTCOME = "$WEATHER_MARKER.outcome"
        private const val COMPLETE = "complete"
        private const val IMPORTED_SINGLE = "imported-single"
        private const val IMPORTED_IDENTICAL = "imported-identical"
        private const val PRESERVED_CURRENT = "preserved-current"
        private const val CONFLICT = "conflict"
        private const val INVALID_SOURCE = "invalid-source"
        private const val NO_SOURCE = "no-source"
        private const val SEPARATOR = "\u001f"
        private val LEGACY_ADDRESS = Regex("[0-9a-fA-F]{2}(?:_[0-9a-fA-F]{2}){5}")
    }
}
