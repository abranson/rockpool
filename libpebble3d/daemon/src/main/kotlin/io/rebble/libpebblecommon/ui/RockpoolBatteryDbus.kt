/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.connection.bt.ble.bluez.BluezManager
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withTimeoutOrNull
import org.freedesktop.dbus.DBusPath
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.interfaces.DBus
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.interfaces.ObjectManager
import org.freedesktop.dbus.interfaces.Properties
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import kotlin.time.Duration.Companion.seconds

private const val BLUEZ_DEVICE = "org.bluez.Device1"
private const val BLUEZ_BATTERY = "org.bluez.Battery1"

internal fun bluezBatteryPercentages(
    objects: Map<DBusPath, Map<String, Map<String, Variant<*>>>>,
    adapterPath: String,
): Map<String, Int> = objects.mapNotNull { (path, interfaces) ->
    if (!path.path.startsWith("$adapterPath/dev_")) return@mapNotNull null
    val device = interfaces[BLUEZ_DEVICE] ?: return@mapNotNull null
    if (device["Connected"]?.value != true) return@mapNotNull null
    val address = (device["Address"]?.value as? String)?.uppercase() ?: return@mapNotNull null
    val raw = interfaces[BLUEZ_BATTERY]?.get("Percentage")?.value as? Number
    // D-Bus BYTE can arrive as signed java.lang.Byte. Reject out-of-range readings.
    val percentage = validBatteryPercentage(raw?.toInt()) ?: return@mapNotNull null
    address to percentage
}.toMap()

/** Observe BlueZ's cached Battery1 data without opening another watch GATT subscription. */
internal fun bluezBatteryLevels(): Flow<Map<String, Int>> = flow {
    val logger = Logger.withTag("RockpoolBattery")
    emit(emptyMap())
    while (currentCoroutineContext().isActive) {
        try {
            val connection = DBusConnectionBuilder.forSystemBus().withShared(false).build()
            try {
                val refresh = Channel<Unit>(Channel.CONFLATED)
                connection.addSigHandler(Properties.PropertiesChanged::class.java) { signal ->
                    if (signal.interfaceName == BLUEZ_DEVICE || signal.interfaceName == BLUEZ_BATTERY ||
                        signal.interfaceName == "org.bluez.Adapter1") {
                        refresh.trySend(Unit)
                    }
                }
                connection.addSigHandler(ObjectManager.InterfacesAdded::class.java) { refresh.trySend(Unit) }
                connection.addSigHandler(ObjectManager.InterfacesRemoved::class.java) { refresh.trySend(Unit) }
                connection.addSigHandler(DBus.NameOwnerChanged::class.java) { signal ->
                    if (signal.name == "org.bluez") refresh.trySend(Unit)
                }
                val manager = connection.getRemoteObject("org.bluez", "/", ObjectManager::class.java, false)
                while (currentCoroutineContext().isActive && connection.isConnected) {
                    val adapter = BluezManager.defaultAdapter()?.dbusPath
                    emit(
                        if (adapter == null) emptyMap()
                        else bluezBatteryPercentages(manager.GetManagedObjects(), adapter),
                    )
                    // Also reconcile adapter selection and retry failed notifications periodically.
                    withTimeoutOrNull(60.seconds) { refresh.receive() }
                }
            } finally {
                connection.disconnect()
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logger.w { "BlueZ battery information unavailable: ${e.message}" }
        }
        emit(emptyMap())
        delay(5.seconds)
    }
}

@DBusInterfaceName("org.freedesktop.Notifications")
internal interface RockpoolBatteryNotifications : DBusInterface {
    fun CloseNotification(id: UInt32)
    fun Notify(
        appName: String, replacesId: UInt32, appIcon: String, summary: String,
        body: String, actions: List<String>, hints: Map<String, Variant<*>>, expireTimeout: Int,
    ): UInt32
}

private fun batteryNotificationKey(address: String) =
    "${address.uppercase().replace(':', '_')}.battery.notification"

internal suspend fun notifyLowWatchBattery(
    settings: RockpoolSettings, address: String, name: String, percentage: Int,
): Boolean {
    val logger = Logger.withTag("RockpoolBattery")
    return try {
        val connection = DBusConnectionBuilder.forSessionBus().withShared(false).build()
        try {
            val summary = "Watch battery low"
            val body = "$name has $percentage% battery remaining."
            val bus = connection.getRemoteObject(
                "org.freedesktop.DBus", "/org/freedesktop/DBus", DBus::class.java,
            )
            val owner = bus.GetNameOwner("org.freedesktop.Notifications")
            val busId = bus.GetId()
            val notifications = connection.getRemoteObject(
                owner, "/org/freedesktop/Notifications",
                RockpoolBatteryNotifications::class.java,
            )
            val id = notifications.Notify(
                "Rockpool", UInt32(0), "rockpool", summary, body, emptyList(),
                mapOf(
                    "desktop-entry" to Variant("rockpool"),
                    "category" to Variant("device"),
                    "x-nemo-preview-summary" to Variant(summary),
                    "x-nemo-preview-body" to Variant(body),
                ), -1,
            ).toLong()
            if (id == 0L) false
            else if (settings.setChecked(batteryNotificationKey(address), "$busId $owner $id")) true
            else {
                notifications.CloseNotification(UInt32(id))
                false
            }
        } finally {
            connection.disconnect()
        }
    } catch (e: CancellationException) {
        throw e
    } catch (e: Exception) {
        logger.w { "low-battery notification failed: ${e.message}" }
        false
    }
}

/** IDs belong to a notification-server instance; never close an ID reused by a new owner. */
internal suspend fun dismissLowWatchBattery(settings: RockpoolSettings, address: String): Boolean {
    val key = batteryNotificationKey(address)
    val record = settings.get(key)
    if (record.isEmpty()) return true
    return try {
        val parts = record.split(' ')
        val id = parts.getOrNull(2)?.toLongOrNull()?.takeIf { it in 1..0xffffffffL }
        val connection = DBusConnectionBuilder.forSessionBus().withShared(false).build()
        try {
            val bus = connection.getRemoteObject(
                "org.freedesktop.DBus", "/org/freedesktop/DBus", DBus::class.java,
            )
            val owner = bus.GetNameOwner("org.freedesktop.Notifications")
            val busId = bus.GetId()
            if (id != null && parts.getOrNull(0) == busId && parts.getOrNull(1) == owner) {
                connection.getRemoteObject(
                    owner, "/org/freedesktop/Notifications", RockpoolBatteryNotifications::class.java,
                ).CloseNotification(UInt32(id))
            }
            settings.setChecked(key, "")
        } finally {
            connection.disconnect()
        }
    } catch (e: CancellationException) {
        throw e
    } catch (e: Exception) {
        Logger.withTag("RockpoolBattery").w { "low-battery notification withdrawal failed: ${e.message}" }
        false
    }
}
