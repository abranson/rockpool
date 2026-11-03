/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import kotlinx.coroutines.CancellationException
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.util.Base64

/** Nemo remote-action arguments use QDataStream's QVariant(QString) representation. */
internal fun firmwareNotificationAction(address: String): String =
    rockpoolNotificationAction("OpenFirmwareUpdate", address)

internal fun rockpoolNotificationAction(method: String, address: String, vararg arguments: String): String {
    require(Regex("[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}").matches(address))
    val encodedArguments = (listOf(address.uppercase()) + arguments).map { argument ->
        val bytes = ByteArrayOutputStream()
        DataOutputStream(bytes).use {
            val text = argument.toByteArray(Charsets.UTF_16BE)
            it.writeInt(10) // QMetaType::QString
            it.writeByte(0) // non-null QVariant
            it.writeInt(text.size)
            it.write(text)
        }
        Base64.getEncoder().encodeToString(bytes.toByteArray())
    }
    return "org.rockpool.UI /org/rockpool/UI org.rockpool.UI $method " +
        encodedArguments.joinToString(" ")
}

internal class RockpoolFirmwareNotification(
    private val settings: RockpoolSettings,
    private val publish: suspend (String, String, String, () -> Boolean) -> Boolean =
        ::publishFirmwareNotification,
) {
    suspend fun update(address: String, name: String, status: RockpoolFirmwareStatus, upgrading: Boolean) {
        if (!status.available || upgrading || status.candidateVersion.isBlank()) return
        val key = "${address.uppercase().replace(':', '_')}.firmware.notifiedVersion"
        if (settings.get(key) == status.candidateVersion) return
        publish(address, name, status.candidateVersion) {
            settings.setChecked(key, status.candidateVersion)
        }
    }
}

private suspend fun publishFirmwareNotification(
    address: String, name: String, version: String, remember: () -> Boolean,
): Boolean = try {
    val connection = DBusConnectionBuilder.forSessionBus().withShared(false).build()
    try {
        val notifications = connection.getRemoteObject(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            RockpoolBatteryNotifications::class.java,
        )
        val summary = "Pebble update available"
        val body = "$name — Firmware $version"
        val id = notifications.Notify(
            "Rockpool", UInt32(0), "rockpool", summary, body,
            listOf("default", "Open firmware update"),
            mapOf(
                "desktop-entry" to Variant("rockpool"),
                "category" to Variant("device"),
                "x-nemo-preview-summary" to Variant(summary),
                "x-nemo-preview-body" to Variant(body),
                "x-nemo-remote-action-default" to Variant(firmwareNotificationAction(address)),
            ), -1,
        )
        if (id.toLong() == 0L) false
        else if (remember()) true
        else {
            notifications.CloseNotification(id)
            false
        }
    } finally {
        connection.disconnect()
    }
} catch (e: CancellationException) {
    throw e
} catch (e: Exception) {
    Logger.withTag("RockpoolFirmware").w { "firmware notification failed: ${e.message}" }
    false
}
