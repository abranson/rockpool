/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.messaging.SendMessageResult
import io.rebble.libpebblecommon.messaging.SendTextFavorite
import io.rebble.libpebblecommon.messaging.SystemMessaging
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.uuid.Uuid

class SendTextConfigurationCoordinatorTest {
    @Test
    fun `compatibility ids match the historical Send Text projection`() {
        val route = "/org/freedesktop/Telepathy/Account/ring/tel/ril_0:+358123"

        assertEquals(
            Uuid.parse("c0c22c84-097a-5be9-a37b-8a4dab530f79"),
            rockpoolSendTextContactUuid("Alice"),
        )
        assertEquals(
            Uuid.parse("2054592a-1604-547a-bfb2-c8bb0b360202"),
            rockpoolSendTextMethodUuid(route),
        )
    }

    @Test
    fun `primary replacement commits durable favorites and activates exact historical ids`() =
        runBlocking {
            withCoordinator { coordinator, settings, projected, _ ->
                var commits = 0
                var changes = 0
                coordinator.addListener { changes++ }

                val result = coordinator.replaceFavorites(
                    mapOf(
                        "Alice" to listOf(
                            "/org/freedesktop/Telepathy/Account/ring/tel/ril_0:+358123",
                        ),
                    ),
                ) {
                    commits++
                    true
                }

                assertEquals(SendTextFavoritesUpdateResult.Completed, result)
                assertEquals(1, commits)
                assertEquals(1, changes)
                assertEquals("Alice", settings.get("contacts.keys"))
                assertEquals(1, projected.single().routes.size)
                assertEquals(
                    Uuid.parse("2054592a-1604-547a-bfb2-c8bb0b360202"),
                    projected.single().routes.single().methodId,
                )
                assertEquals(
                    "2054592a-1604-547a-bfb2-c8bb0b360202",
                    sendTextFavoriteRecords(coordinator.favorites()).single()["methodId"],
                )
            }
        }

    @Test
    fun `primary send resolves only a currently configured method`() = runBlocking {
        withCoordinator { coordinator, _, _, messages ->
            val route = "/org/freedesktop/Telepathy/Account/ring/tel/ril_0:+358123"
            assertEquals(
                SendTextFavoritesUpdateResult.Completed,
                coordinator.replaceFavorites(mapOf("Alice" to listOf(route))) { true },
            )

            assertEquals(
                SendTextDispatchResult.Sent,
                coordinator.sendText(rockpoolSendTextMethodUuid(route), "Hello"),
            )
            assertEquals(
                Triple(
                    "/org/freedesktop/Telepathy/Account/ring/tel/ril_0",
                    "+358123",
                    "Hello",
                ),
                messages.single(),
            )
            assertEquals(
                SendTextDispatchResult.UnknownMethod,
                coordinator.sendText(Uuid.random(), "Hello"),
            )
            assertEquals(1, messages.size)
        }
    }

    @Test
    fun `invalid replacement is rejected before the commit boundary`() = runBlocking {
        withCoordinator { coordinator, settings, projected, _ ->
            var committed = false
            val result = coordinator.replaceFavorites(
                mapOf("Alice" to listOf("not-a-telepathy-route")),
            ) {
                committed = true
                true
            }

            assertEquals(SendTextFavoritesUpdateResult.InvalidArgument, result)
            assertFalse(committed)
            assertTrue(settings.entries("contacts.").isEmpty())
            assertTrue(projected.isEmpty())
        }
    }

    private suspend fun withCoordinator(
        test: suspend (
            SendTextConfigurationCoordinator,
            RockpoolSettings,
            MutableList<SendTextFavorite>,
            MutableList<Triple<String, String, String>>,
        ) -> Unit,
    ) {
        val directory = Files.createTempDirectory("send-text-coordinator-")
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val projected = mutableListOf<SendTextFavorite>()
        val messages = mutableListOf<Triple<String, String, String>>()
        val messaging = object : SystemMessaging {
            override suspend fun sendMessage(
                accountId: String,
                recipient: String,
                text: String,
            ): SendMessageResult {
                messages += Triple(accountId, recipient, text)
                return SendMessageResult.Sent
            }
        }
        try {
            val settings = RockpoolSettings(directory.resolve("rockpool.properties"))
            val coordinator = SendTextConfigurationCoordinator(
                replaceConfiguration = { favorites, _ ->
                    projected.clear()
                    projected += favorites
                },
                systemMessaging = messaging,
                settings = settings,
                scope = scope,
            )
            test(coordinator, settings, projected, messages)
        } finally {
            scope.cancel()
            directory.toFile().deleteRecursively()
        }
    }
}
