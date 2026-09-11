/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withTimeoutOrNull
import org.freedesktop.dbus.Struct
import org.freedesktop.dbus.Tuple
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.annotations.DBusMemberName
import org.freedesktop.dbus.annotations.Position
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.interfaces.DBusInterface

internal class GeoClueAccuracy(
    @field:Position(0) val level: Int,
    @field:Position(1) val horizontal: Double,
    @field:Position(2) val vertical: Double,
) : Struct()

internal class GeoClueReverseAddressReply(
    @field:Position(0) val details: Map<String, String>,
    accuracyValues: Array<Any>,
) : Tuple() {
    @field:Position(1)
    val accuracy = GeoClueAccuracy(
        level = (accuracyValues[0] as Number).toInt(),
        horizontal = (accuracyValues[1] as Number).toDouble(),
        vertical = (accuracyValues[2] as Number).toDouble(),
    )
}

@Suppress("FunctionName")
@DBusInterfaceName("org.freedesktop.Geoclue.ReverseGeocode")
internal interface GeoClueReverseGeocode : DBusInterface {
    @DBusMemberName("PositionToAddress")
    fun PositionToAddress(
        latitude: Double,
        longitude: Double,
        positionAccuracy: GeoClueAccuracy,
    ): GeoClueReverseAddressReply
}

@Suppress("FunctionName")
@DBusInterfaceName("org.freedesktop.Geoclue")
internal interface GeoClueClientLifecycle : DBusInterface {
    @DBusMemberName("AddReference")
    fun AddReference()

    @DBusMemberName("RemoveReference")
    fun RemoveReference()
}

internal fun interface GeoClueReverseSource {
    suspend fun addressFor(coordinates: RockpoolWeatherCoordinates): Map<String, String>?
}

/** Resolves Rockpool's already-acquired weather coordinates through the optional offline provider. */
internal class GeoClueCurrentLocationNameResolver(
    private val timeoutMillis: Long = GEOCLUE_LOOKUP_TIMEOUT_MILLIS,
    private val source: GeoClueReverseSource = DbusGeoClueReverseSource(),
) {
    suspend fun resolve(coordinates: RockpoolWeatherCoordinates): String? =
        withTimeoutOrNull(timeoutMillis) {
            try {
                source.addressFor(coordinates)
                    ?.get(GEOCLUE_LOCALITY_KEY)
                    ?.trim()
                    ?.takeIf(::validLocationName)
            } catch (e: CancellationException) {
                throw e
            } catch (_: Exception) {
                null
            }
        }
}

internal class DbusGeoClueReverseSource(
    private val connect: () -> org.freedesktop.dbus.connections.impl.DBusConnection = {
        DBusConnectionBuilder.forSessionBus().apply {
            transportConfig().withTimeout(GEOCLUE_LOOKUP_TIMEOUT_MILLIS.toInt()).back()
        }.withShared(false).build()
    },
    private val service: String = GEOCLUE_GEONAMES_SERVICE,
    private val path: String = GEOCLUE_GEONAMES_PATH,
) : GeoClueReverseSource {
    override suspend fun addressFor(
        coordinates: RockpoolWeatherCoordinates,
    ): Map<String, String>? = runInterruptible(Dispatchers.IO) {
        val connection = connect()
        try {
            val lifecycle = connection.getRemoteObject(
                service,
                path,
                GeoClueClientLifecycle::class.java,
                false,
            )
            lifecycle.AddReference()
            try {
                connection.getRemoteObject(
                    service,
                    path,
                    GeoClueReverseGeocode::class.java,
                    false,
                ).PositionToAddress(
                    latitude = coordinates.latitude,
                    longitude = coordinates.longitude,
                    positionAccuracy = GeoClueAccuracy(
                        level = GEOCLUE_ACCURACY_DETAILED,
                        horizontal = coordinates.horizontalAccuracy
                            ?.takeIf { it.isFinite() && it >= 0.0 }
                            ?: 0.0,
                        vertical = 0.0,
                    ),
                ).details
            } finally {
                runCatching { lifecycle.RemoveReference() }
            }
        } finally {
            runCatching { connection.disconnect() }
        }
    }
}

private fun validLocationName(name: String): Boolean =
    name.isNotBlank() && '\u0000' !in name && name.encodeToByteArray().size <= MAX_LOCATION_NAME_BYTES

private const val GEOCLUE_GEONAMES_SERVICE =
    "org.freedesktop.Geoclue.Providers.GeonamesOffline"
private const val GEOCLUE_GEONAMES_PATH =
    "/org/freedesktop/Geoclue/Providers/GeonamesOffline"
private const val GEOCLUE_ACCURACY_DETAILED = 6
private const val GEOCLUE_LOCALITY_KEY = "locality"
private const val GEOCLUE_LOOKUP_TIMEOUT_MILLIS = 2_000L
private const val MAX_LOCATION_NAME_BYTES = 128
