/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.bt.LinuxPairingRequester
import io.rebble.libpebblecommon.connection.bt.ble.bluez.LinuxBluezAdapter
import io.rebble.libpebblecommon.connection.bt.ble.bluez.LinuxBluezAdapterSelector
import kotlinx.coroutines.runBlocking

/** Delegates pairing dispatch to the platform bridge; completion remains with BlueZ. */
internal class SailfishPairingRequester(
    private val request: suspend (String) -> Boolean,
) : LinuxPairingRequester {
    override fun requestPairing(address: String): Boolean = runBlocking { request(address) }
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
