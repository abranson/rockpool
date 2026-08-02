/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RockpoolSettingsTest {
    @Test
    fun `atomic prefix updates cannot lose a concurrent record`() {
        val directory = Files.createTempDirectory("rockpool-settings-prefix-")
        try {
            val settings = RockpoolSettings(directory.resolve("rockpool.properties"))
            assertTrue(
                settings.replacePrefix(
                    "canned.",
                    mapOf("canned.keys" to "base", "canned.base" to "Initial"),
                ),
            )

            val firstEntered = CountDownLatch(1)
            val releaseFirst = CountDownLatch(1)
            val secondEntered = CountDownLatch(1)
            val failure = AtomicReference<Throwable?>()

            val first = thread(name = "rockpool-settings-first") {
                runCatching {
                    check(
                        settings.updatePrefixChecked("canned.") { current ->
                            firstEntered.countDown()
                            check(releaseFirst.await(5, TimeUnit.SECONDS))
                            current + ("canned.first" to "One")
                        },
                    )
                }.onFailure(failure::compareAndSetNull)
            }
            assertTrue(firstEntered.await(5, TimeUnit.SECONDS))

            val second = thread(name = "rockpool-settings-second") {
                runCatching {
                    check(
                        settings.updatePrefixChecked("canned.") { current ->
                            secondEntered.countDown()
                            check(current["canned.first"] == "One")
                            current + ("canned.second" to "Two")
                        },
                    )
                }.onFailure(failure::compareAndSetNull)
            }

            assertFalse(secondEntered.await(100, TimeUnit.MILLISECONDS))
            releaseFirst.countDown()
            first.join(5_000)
            second.join(5_000)

            assertFalse(first.isAlive)
            assertFalse(second.isAlive)
            assertNull(failure.get())
            assertEquals(
                mapOf(
                    "canned.keys" to "base",
                    "canned.base" to "Initial",
                    "canned.first" to "One",
                    "canned.second" to "Two",
                ),
                settings.entries("canned."),
            )
        } finally {
            directory.toFile().deleteRecursively()
        }
    }
}

private fun AtomicReference<Throwable?>.compareAndSetNull(failure: Throwable) {
    compareAndSet(null, failure)
}
