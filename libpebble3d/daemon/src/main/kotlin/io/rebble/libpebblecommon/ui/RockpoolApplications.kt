/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.locker.SystemApps
import io.rebble.libpebblecommon.locker.findCompatiblePlatform
import io.rebble.libpebblecommon.metadata.WatchType
import io.rebble.libpebblecommon.rockpool.rockpoolApplicationIsInstalledOnWatch
import org.freedesktop.dbus.types.Variant
import kotlin.uuid.Uuid

internal fun formatRockpoolAppUuid(uuid: Uuid): String = "{$uuid}"

/**
 * libpebble3 currently stores one account-global desired locker.  The old Rockpool object was
 * watch-scoped, so a mutation is faithful only while the addressed watch is the sole known watch.
 */
internal fun rockpoolGlobalAppMutationAllowed(
    targetAddress: String,
    knownAddresses: List<String>,
): Boolean = knownAddresses
    .map(String::uppercase)
    .distinct()
    .let { it.size == 1 && it.single() == targetAddress.uppercase() }

internal fun rockpoolInstalledApplications(
    applications: List<LockerWrapper>,
    watchType: WatchType,
): List<LockerWrapper> = applications
    .filter { rockpoolApplicationIsInstalledOnWatch(it, watchType) }
    .sortedBy { it.properties.order }

internal fun rockpoolApplicationRecords(
    applications: List<LockerWrapper>,
    watchType: WatchType,
): List<Variant<*>> = rockpoolInstalledApplications(applications, watchType).map { app ->
    val properties = app.properties
    val platform = checkNotNull(app.findCompatiblePlatform(watchType))
    Variant(
        linkedMapOf(
            "uuid" to Variant(formatRockpoolAppUuid(properties.id)),
            "storeId" to Variant(properties.storeId.orEmpty()),
            "name" to Variant(properties.title),
            "vendor" to Variant(properties.developerName),
            "watchface" to Variant(properties.type == AppType.Watchface),
            "version" to Variant(properties.version.orEmpty()),
            "hasSettings" to Variant((app as? LockerWrapper.NormalApp)?.configurable ?: false),
            "icon" to Variant(platform.iconImageUrl.orEmpty()),
            "systemApp" to Variant(app is LockerWrapper.SystemApp),
        ),
        "a{sv}",
    )
}

internal fun resolveRockpoolInstalledApplication(
    applications: List<LockerWrapper>,
    idOrStoreId: String,
): LockerWrapper? {
    val uuid = runCatching { parseRockpoolAppUuid(idOrStoreId) }.getOrNull()
    return if (uuid != null) {
        applications.firstOrNull { it.properties.id == uuid }
    } else {
        applications.firstOrNull { it.properties.storeId == idOrStoreId }
    }
}

internal fun validateRockpoolAppOrder(
    values: List<String>,
    installedApplications: List<LockerWrapper>,
): List<Uuid>? {
    if (values.size != installedApplications.size) return null
    val parsed = values.map { value ->
        runCatching { parseRockpoolAppUuid(value) }.getOrNull() ?: return null
    }
    if (parsed.distinct().size != parsed.size) return null
    if (parsed.toSet() != installedApplications.mapTo(hashSetOf()) { it.properties.id }) return null
    if (parsed.firstOrNull() != SystemApps.Settings.uuid) return null
    return parsed
}
