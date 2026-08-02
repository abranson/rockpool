/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.connection.bt.LinuxPairingRequester
import io.rebble.libpebblecommon.connection.bt.ble.bluez.LinuxBluezAdapter
import io.rebble.libpebblecommon.connection.bt.ble.bluez.LinuxBluezAdapterSelector
import io.rebble.libpebblecommon.linux.dbus.RawSessionConnection

/** Delegates the typed bond request to Sailfish's session pairing broker. */
internal class SailfishPairingRequester : LinuxPairingRequester {
    private val logger = Logger.withTag("SailfishPairingRequester")

    override fun requestPairing(address: String): Boolean = try {
        RawSessionConnection.connect()?.use { connection ->
            connection.callWithReply(
                "com.jolla.lipstick",
                "/bluetooth",
                "com.jolla.lipstick",
                "pairWithDevice",
                "s",
                address,
            ) != null
        } ?: false
    } catch (e: Throwable) {
        logger.e("Sailfish pairing request failed for $address", e)
        false
    }
}

/** Prefer AlienBT's later virtual HCI for a duplicated controller address. */
internal class SailfishBluezAdapterSelector : LinuxBluezAdapterSelector {
    override fun select(adapters: List<LinuxBluezAdapter>): String? {
        val deduplicated = adapters
            .groupBy { it.address.uppercase() }
            .values
            .mapNotNull { duplicates -> duplicates.maxByOrNull { adapterIndex(it.path) } }
        return deduplicated
            .filter { it.powered }
            .ifEmpty { deduplicated }
            .firstOrNull()
            ?.path
    }

    private fun adapterIndex(path: String): Int =
        path.substringAfterLast("hci", missingDelimiterValue = "")
            .toIntOrNull()
            ?: -1
}
