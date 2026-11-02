/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class RockpoolFirmwareNotificationTest {
    private val address = "01:23:45:67:89:AB"
    private val available = RockpoolFirmwareStatus(true, "v4.42.0", "Notes")

    @Test fun `same candidate stays quiet across reconnects and daemon restart`() = runBlocking {
        val dir = Files.createTempDirectory("firmware-notification")
        try {
            val path = dir.resolve("settings")
            val sent = mutableListOf<Pair<String, String>>()
            val publish: suspend (String, String, String, () -> Boolean) -> Boolean = { id, _, version, save ->
                sent.add(id to version)
                save()
            }
            val notification = RockpoolFirmwareNotification(RockpoolSettings(path), publish)
            notification.update(address, "Pebble", available, false)
            notification.update(address, "Pebble", available.copy(available = false), false)
            notification.update(address, "Pebble", available.copy(releaseNotes = "New notes"), false)
            val restarted = RockpoolFirmwareNotification(RockpoolSettings(path), publish)
            restarted.update(address.lowercase(), "Pebble", available, false)
            assertEquals(1, sent.size)
            restarted.update(address, "Pebble", available.copy(candidateVersion = "v4.43.0"), false)
            restarted.update("01:23:45:67:89:AC", "Other watch", available, false)
            assertEquals(3, sent.size)
        } finally { dir.toFile().deleteRecursively() }
    }

    @Test fun `failed delivery retries and unavailable or installing states never notify`() = runBlocking {
        val dir = Files.createTempDirectory("firmware-notification")
        try {
            var calls = 0
            val notification = RockpoolFirmwareNotification(RockpoolSettings(dir.resolve("settings"))) { _, _, _, save ->
                calls++
                if (calls == 1) false else save()
            }
            notification.update(address, "Pebble", available.copy(available = false), false)
            notification.update(address, "Pebble", available, true)
            notification.update(address, "Pebble", available.copy(candidateVersion = ""), false)
            assertEquals(0, calls)
            repeat(3) { notification.update(address, "Pebble", available, false) }
            assertEquals(2, calls)
        } finally { dir.toFile().deleteRecursively() }
    }

    @Test fun `notification tap uses a typed QString watch address`() {
        assertEquals(
            "org.rockpool.UI /org/rockpool/UI org.rockpool.UI OpenFirmwareUpdate " +
                "AAAACgAAAAAiADAAMQA6ADIAMwA6ADQANQA6ADYANwA6ADgAOQA6AEEAQg==",
            firmwareNotificationAction(address.lowercase()),
        )
        assertFailsWith<IllegalArgumentException> { firmwareNotificationAction("not an address") }
    }
}
