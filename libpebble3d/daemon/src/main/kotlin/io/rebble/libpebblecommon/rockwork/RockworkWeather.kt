/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.compat.rockwork

import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.weather.WeatherLocationData
import io.rebble.libpebblecommon.weather.WeatherType
import org.freedesktop.dbus.types.UInt16
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.UInt64
import org.freedesktop.dbus.types.Variant
import java.nio.ByteBuffer
import java.security.MessageDigest
import java.time.Instant
import java.time.LocalDateTime
import java.time.OffsetDateTime
import java.time.ZoneId
import kotlin.uuid.Uuid

internal data class RockworkWeatherLocation(
    val key: Uuid,
    val name: String,
    val latitude: String,
    val longitude: String,
    val observation: RockworkWeatherObservation? = null,
)

internal data class RockworkWeatherObservation(
    val text: String,
    val temperature: Short,
    val timestampEpochSeconds: Long,
    val todayHigh: Short,
    val todayLow: Short,
    val todayIcon: WeatherType,
    val tomorrowHigh: Short,
    val tomorrowLow: Short,
    val tomorrowIcon: WeatherType,
    val source: RockworkWeatherObservationSource = RockworkWeatherObservationSource.EXTERNAL,
)

internal enum class RockworkWeatherObservationSource {
    EXTERNAL,
    AUTOMATIC,
}

internal const val ROCKWORK_WEATHER_SETTINGS_PREFIX = "weather.locations."
internal const val ROCKWORK_MAX_WEATHER_LOCATIONS = 6

internal data class RockworkWeatherFetchTarget(
    val key: Uuid,
    val name: String,
    val latitude: String,
    val longitude: String,
    val coordinates: RockworkWeatherCoordinates?,
    val currentLocation: Boolean,
)

internal data class RockworkWeatherCoordinates(
    val latitude: Double,
    val longitude: Double,
)

/**
 * Owns compatibility weather locations, external injection and supported automatic forecasts.
 *
 * libpebble3's weather database is account-global, so this coordinator is deliberately shared by
 * every compatibility watch object. Calls are serialized and every update submits one complete
 * snapshot, preventing an update for one location from deleting the others. An external injection
 * remains authoritative for that location until its coordinates change.
 */
internal class RockworkWeatherCoordinator(
    private val loadSettings: () -> Map<String, String>,
    private val replaceSettings: (Map<String, String>) -> Boolean,
    private val updateWeatherData: (List<WeatherLocationData>) -> Unit,
    private val nowEpochSeconds: () -> Long = { System.currentTimeMillis() / 1_000L },
) {
    constructor(settings: RockpoolSettings, libPebble: LibPebble) : this(
        loadSettings = { settings.entries(ROCKWORK_WEATHER_SETTINGS_PREFIX) },
        replaceSettings = {
            settings.replacePrefix(ROCKWORK_WEATHER_SETTINGS_PREFIX, it)
        },
        updateWeatherData = libPebble::updateWeatherData,
    )

    private var state: List<RockworkWeatherLocation> =
        decodeRockworkWeatherSettings(loadSettings()).getOrDefault(emptyList())

    init {
        // RockpoolSettings is the durable compatibility source of truth. Reconcile it with
        // libpebble3 on every daemon start in case the previous asynchronous update was
        // interrupted after the settings commit.
        updateWeatherData(state.toWeatherData())
    }

    @Synchronized
    fun locations(): List<Variant<*>> = state.map { location ->
        Variant(listOf(location.name, location.latitude, location.longitude), "as")
    }

    /** Reconciles a late legacy migration into the live coordinator without a restart. */
    @Synchronized
    fun reloadPersisted(): Result<Boolean> {
        val persisted = decodeRockworkWeatherSettings(loadSettings()).getOrElse {
            return Result.failure(it)
        }
        if (persisted == state) return Result.success(false)
        state = persisted
        updateWeatherData(state.toWeatherData())
        return Result.success(true)
    }

    @Synchronized
    fun setLocations(values: List<Variant<*>>): Boolean {
        val parsed = parseRockworkWeatherLocations(values)
        val previous = state.associateBy { it.name }
        val replacement = parsed.map { location ->
            val existing = previous[location.name]
            location.copy(
                observation = existing?.observation?.takeIf {
                    existing.latitude == location.latitude &&
                        existing.longitude == location.longitude
                },
            )
        }
        if (!replaceSettings(encodeRockworkWeatherSettings(replacement))) return false
        state = replacement
        updateWeatherData(state.toWeatherData())
        return true
    }

    /** Locations whose observations may be populated by the supported weather fetcher. */
    @Synchronized
    fun automaticFetchTargets(): List<RockworkWeatherFetchTarget> = state.mapIndexedNotNull { index, location ->
        if (location.observation?.source == RockworkWeatherObservationSource.EXTERNAL) {
            return@mapIndexedNotNull null
        }
        val currentLocation = index == 0 && location.latitude == CURRENT_COORDINATE &&
            location.longitude == CURRENT_COORDINATE
        if (currentLocation) {
            return@mapIndexedNotNull RockworkWeatherFetchTarget(
                key = location.key,
                name = location.name,
                latitude = location.latitude,
                longitude = location.longitude,
                coordinates = null,
                currentLocation = true,
            )
        }
        val latitude = location.latitude.toDoubleOrNull()?.takeIf(Double::isFinite)
            ?: return@mapIndexedNotNull null
        val longitude = location.longitude.toDoubleOrNull()?.takeIf(Double::isFinite)
            ?: return@mapIndexedNotNull null
        if (latitude !in -90.0..90.0 || longitude !in -180.0..180.0) {
            return@mapIndexedNotNull null
        }
        RockworkWeatherFetchTarget(
            key = location.key,
            name = location.name,
            latitude = location.latitude,
            longitude = location.longitude,
            coordinates = RockworkWeatherCoordinates(latitude, longitude),
            currentLocation = false,
        )
    }

    /**
     * Applies a fetched observation only while the exact location still exists and has not been
     * superseded by the external injection API during the network request.
     */
    @Synchronized
    fun applyAutomaticObservation(
        target: RockworkWeatherFetchTarget,
        observation: RockworkWeatherObservation,
    ): Boolean {
        val index = state.indexOfFirst {
            it.key == target.key && it.name == target.name &&
                it.latitude == target.latitude && it.longitude == target.longitude
        }
        if (index < 0 || state[index].observation?.source == RockworkWeatherObservationSource.EXTERNAL) {
            return false
        }
        val replacement = state.toMutableList().also {
            it[index] = it[index].copy(
                observation = observation.copy(source = RockworkWeatherObservationSource.AUTOMATIC),
            )
        }
        if (!replaceSettings(encodeRockworkWeatherSettings(replacement))) return false
        state = replacement
        updateWeatherData(state.toWeatherData())
        return true
    }

    @Synchronized
    fun inject(locationName: String, conditions: Map<String, Variant<*>>): Boolean {
        val index = state.indexOfFirst { it.name == locationName }
        require(index >= 0) { "Unknown weather location" }
        val observation = parseRockworkWeatherObservation(conditions, nowEpochSeconds)
        val replacement = state.toMutableList().also {
            it[index] = it[index].copy(observation = observation)
        }
        if (!replaceSettings(encodeRockworkWeatherSettings(replacement))) return false
        state = replacement
        updateWeatherData(state.toWeatherData())
        return true
    }
}

internal fun parseRockworkWeatherLocations(
    values: List<Variant<*>>,
): List<RockworkWeatherLocation> {
    require(values.size <= ROCKWORK_MAX_WEATHER_LOCATIONS) { "Too many weather locations" }
    val seenNames = hashSetOf<String>()
    return values.mapIndexed { index, variant ->
        require(variant.sig == "as") { "Weather location must be an array of strings" }
        val fields = when (val raw = variant.value) {
            is List<*> -> raw
            is Array<*> -> raw.toList()
            else -> throw IllegalArgumentException("Weather location must be an array of strings")
        }
        require(fields.size == 3 && fields.all { it is String }) {
            "Weather location must contain name, latitude and longitude"
        }
        val name = fields[0] as String
        val latitude = fields[1] as String
        val longitude = fields[2] as String
        requireWeatherString(name, MAX_LOCATION_NAME_BYTES, "Weather location name")
        requireWeatherString(latitude, MAX_COORDINATE_BYTES, "Weather latitude")
        requireWeatherString(longitude, MAX_COORDINATE_BYTES, "Weather longitude")
        require(seenNames.add(name)) { "Weather location names must be unique" }
        val currentPlaceholder = index == 0 && latitude == CURRENT_COORDINATE &&
            longitude == CURRENT_COORDINATE
        if (!currentPlaceholder) {
            val latitudeValue = latitude.toDoubleOrNull()
            val longitudeValue = longitude.toDoubleOrNull()
            require(latitudeValue != null && latitudeValue.isFinite() && latitudeValue in -90.0..90.0) {
                "Weather latitude is invalid"
            }
            require(longitudeValue != null && longitudeValue.isFinite() && longitudeValue in -180.0..180.0) {
                "Weather longitude is invalid"
            }
        }
        RockworkWeatherLocation(
            key = if (index == 0) CURRENT_LOCATION_UUID else rockworkWeatherUuid(name),
            name = name,
            latitude = latitude,
            longitude = longitude,
        )
    }
}

internal fun parseRockworkWeatherObservation(
    conditions: Map<String, Variant<*>>,
    nowEpochSeconds: () -> Long,
): RockworkWeatherObservation {
    val allowedKeys = setOf(
        "text",
        "temperature",
        "ts",
        "today_hi",
        "today_low",
        "today_icon",
        "tomorrow_hi",
        "tomorrow_low",
        "tomorrow_icon",
    )
    require(conditions.keys.all { it in allowedKeys }) { "Unknown weather condition field" }
    val text = conditions["text"]?.let(::rockworkWeatherString)
        ?: throw IllegalArgumentException("Weather text is required")
    requireWeatherString(text, MAX_FORECAST_BYTES, "Weather text")
    val temperature = conditions["temperature"]?.let(::rockworkWeatherShort)
        ?: throw IllegalArgumentException("Weather temperature is required")
    val timestamp = conditions["ts"]?.let(::rockworkWeatherTimestamp) ?: nowEpochSeconds()
    require(timestamp in 0..UInt.MAX_VALUE.toLong()) { "Weather timestamp is out of range" }
    return RockworkWeatherObservation(
        text = text,
        temperature = temperature,
        timestampEpochSeconds = timestamp,
        todayHigh = conditions["today_hi"]?.let(::rockworkWeatherShort) ?: Short.MAX_VALUE,
        todayLow = conditions["today_low"]?.let(::rockworkWeatherShort) ?: Short.MAX_VALUE,
        todayIcon = conditions["today_icon"]?.let(::rockworkWeatherType)
            ?: WeatherType.PartlyCloudy,
        tomorrowHigh = conditions["tomorrow_hi"]?.let(::rockworkWeatherShort) ?: Short.MAX_VALUE,
        tomorrowLow = conditions["tomorrow_low"]?.let(::rockworkWeatherShort) ?: Short.MAX_VALUE,
        tomorrowIcon = conditions["tomorrow_icon"]?.let(::rockworkWeatherType)
            ?: WeatherType.PartlyCloudy,
    )
}

internal fun encodeRockworkWeatherSettings(
    locations: List<RockworkWeatherLocation>,
): Map<String, String> = buildMap {
    put("${ROCKWORK_WEATHER_SETTINGS_PREFIX}count", locations.size.toString())
    locations.forEachIndexed { index, location ->
        val prefix = "$ROCKWORK_WEATHER_SETTINGS_PREFIX$index."
        put("${prefix}key", location.key.toString())
        put("${prefix}name", location.name)
        put("${prefix}latitude", location.latitude)
        put("${prefix}longitude", location.longitude)
        location.observation?.let { observation ->
            put("${prefix}observation", "true")
            put("${prefix}text", observation.text)
            put("${prefix}temperature", observation.temperature.toString())
            put("${prefix}timestamp", observation.timestampEpochSeconds.toString())
            put("${prefix}todayHigh", observation.todayHigh.toString())
            put("${prefix}todayLow", observation.todayLow.toString())
            put("${prefix}todayIcon", observation.todayIcon.code.toUByte().toString())
            put("${prefix}tomorrowHigh", observation.tomorrowHigh.toString())
            put("${prefix}tomorrowLow", observation.tomorrowLow.toString())
            put("${prefix}tomorrowIcon", observation.tomorrowIcon.code.toUByte().toString())
            put("${prefix}source", observation.source.name.lowercase())
        }
    }
}

internal fun decodeRockworkWeatherSettings(
    values: Map<String, String>,
): Result<List<RockworkWeatherLocation>> = runCatching {
    val count = values["${ROCKWORK_WEATHER_SETTINGS_PREFIX}count"]?.toIntOrNull() ?: 0
    require(count in 0..ROCKWORK_MAX_WEATHER_LOCATIONS)
    (0 until count).map { index ->
        val prefix = "$ROCKWORK_WEATHER_SETTINGS_PREFIX$index."
        val location = RockworkWeatherLocation(
            key = Uuid.parse(checkNotNull(values["${prefix}key"])),
            name = checkNotNull(values["${prefix}name"]),
            latitude = checkNotNull(values["${prefix}latitude"]),
            longitude = checkNotNull(values["${prefix}longitude"]),
        )
        val observation = if (values["${prefix}observation"]?.toBooleanStrictOrNull() == true) {
            val text = checkNotNull(values["${prefix}text"])
            val timestamp = checkNotNull(values["${prefix}timestamp"]?.toLongOrNull())
            requireWeatherString(text, MAX_FORECAST_BYTES, "Weather text")
            require(timestamp in 0..UInt.MAX_VALUE.toLong())
            RockworkWeatherObservation(
                text = text,
                temperature = checkNotNull(values["${prefix}temperature"]?.toShortOrNull()),
                timestampEpochSeconds = timestamp,
                todayHigh = checkNotNull(values["${prefix}todayHigh"]?.toShortOrNull()),
                todayLow = checkNotNull(values["${prefix}todayLow"]?.toShortOrNull()),
                todayIcon = weatherType(checkNotNull(values["${prefix}todayIcon"]?.toIntOrNull())),
                tomorrowHigh = checkNotNull(values["${prefix}tomorrowHigh"]?.toShortOrNull()),
                tomorrowLow = checkNotNull(values["${prefix}tomorrowLow"]?.toShortOrNull()),
                tomorrowIcon = weatherType(checkNotNull(values["${prefix}tomorrowIcon"]?.toIntOrNull())),
                source = when (values["${prefix}source"]) {
                    "automatic" -> RockworkWeatherObservationSource.AUTOMATIC
                    else -> RockworkWeatherObservationSource.EXTERNAL
                },
            )
        } else {
            null
        }
        location.copy(observation = observation)
    }.also { locations ->
        // Reuse the public validation for bounds, coordinates, duplicate names and stable keys.
        val validated = parseRockworkWeatherLocations(locations.map {
            Variant(listOf(it.name, it.latitude, it.longitude), "as")
        })
        require(validated.map { it.key } == locations.map { it.key })
    }
}

private fun List<RockworkWeatherLocation>.toWeatherData(): List<WeatherLocationData> =
    mapIndexed { index, location ->
        location.observation?.let { observation ->
            WeatherLocationData.WeatherLocationDataPopulated(
                key = location.key,
                currentTemp = observation.temperature,
                currentWeatherType = observation.todayIcon,
                todayHighTemp = observation.todayHigh,
                todayLowTemp = observation.todayLow,
                tomorrowWeatherType = observation.tomorrowIcon,
                tomorrowHighTemp = observation.tomorrowHigh,
                tomorrowLowTemp = observation.tomorrowLow,
                lastUpdateTimeUtcSecs = observation.timestampEpochSeconds,
                isCurrentLocation = index == 0,
                locationName = location.name,
                forecastShort = observation.text,
            )
        } ?: WeatherLocationData.WeatherLocationDataFailed(location.key)
    }

private fun rockworkWeatherString(variant: Variant<*>): String {
    require(variant.sig == "s") { "Weather text must be a string" }
    return variant.value as? String ?: throw IllegalArgumentException("Weather text must be a string")
}

private fun rockworkWeatherShort(variant: Variant<*>): Short {
    val value = rockworkWeatherInteger(variant)
    require(value in Short.MIN_VALUE.toLong()..Short.MAX_VALUE.toLong()) {
        "Weather temperature is out of range"
    }
    return value.toShort()
}

private fun rockworkWeatherType(variant: Variant<*>): WeatherType {
    val code = rockworkWeatherInteger(variant)
    require(code in 0..Int.MAX_VALUE.toLong()) { "Weather icon is invalid" }
    return weatherType(code.toInt())
}

private fun weatherType(code: Int): WeatherType = WeatherType.entries.firstOrNull {
    it.code.toUByte().toInt() == code && it != WeatherType.Unknown
} ?: throw IllegalArgumentException("Weather icon is invalid")

private fun rockworkWeatherTimestamp(variant: Variant<*>): Long {
    if (variant.sig != "s") return rockworkWeatherInteger(variant)
    val value = variant.value as? String
        ?: throw IllegalArgumentException("Weather timestamp is invalid")
    return runCatching { Instant.parse(value).epochSecond }
        .recoverCatching { OffsetDateTime.parse(value).toEpochSecond() }
        .recoverCatching { LocalDateTime.parse(value).atZone(ZoneId.systemDefault()).toEpochSecond() }
        .getOrElse { throw IllegalArgumentException("Weather timestamp is invalid") }
}

private fun rockworkWeatherInteger(variant: Variant<*>): Long = when (variant.sig) {
    "y" -> (variant.value as? Byte)?.toUByte()?.toLong()
    "n" -> (variant.value as? Short)?.toLong()
    "q" -> (variant.value as? UInt16)?.toInt()?.toLong()
    "i" -> (variant.value as? Int)?.toLong()
    "u" -> (variant.value as? UInt32)?.toLong()
    "x" -> variant.value as? Long
    "t" -> (variant.value as? UInt64)?.let { value ->
        try {
            value.value().longValueExact()
        } catch (_: ArithmeticException) {
            throw IllegalArgumentException("Weather value is out of range")
        }
    }
    else -> null
} ?: throw IllegalArgumentException("Weather value must be an integer")

private fun requireWeatherString(value: String, maximumBytes: Int, field: String) {
    require(value.isNotBlank() && '\u0000' !in value && value.encodeToByteArray().size <= maximumBytes) {
        "$field is invalid"
    }
}

private fun rockworkWeatherUuid(name: String): Uuid {
    val namespace = java.util.UUID.fromString(DNS_NAMESPACE_UUID)
    val bytes = ByteBuffer.allocate(16)
        .putLong(namespace.mostSignificantBits)
        .putLong(namespace.leastSignificantBits)
        .array()
    val digest = MessageDigest.getInstance("SHA-1").run {
        update(bytes)
        digest("$name.pin.rockpool.nemomobile.org".encodeToByteArray())
    }
    digest[6] = ((digest[6].toInt() and 0x0f) or 0x50).toByte()
    digest[8] = ((digest[8].toInt() and 0x3f) or 0x80).toByte()
    val buffer = ByteBuffer.wrap(digest, 0, 16)
    return Uuid.parse(java.util.UUID(buffer.long, buffer.long).toString())
}

private const val MAX_LOCATION_NAME_BYTES = 128
private const val MAX_COORDINATE_BYTES = 64
private const val MAX_FORECAST_BYTES = 256
private const val CURRENT_COORDINATE = "n/a"
private const val DNS_NAMESPACE_UUID = "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
private val CURRENT_LOCATION_UUID = Uuid.parse("5f63c159-1c67-455f-897e-125d70664c4f")
