/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.rockpool.NotificationFilterCoordinator
import io.rebble.libpebblecommon.rockpool.NotificationFilterMutation
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

/**
 * Preserves D-Bus arrival order for the account-global legacy notification mutations.
 *
 * Every exported Pebble object shares this queue. Launching one coroutine per void D-Bus call
 * would let Dispatchers.Default reverse two rapid choices before they reached the coordinator's
 * mutex, allowing the older choice to become the final persisted value.
 */
internal class RockpoolNotificationFilterMutations(
    scope: CoroutineScope,
    private val setFilter: suspend (
        sourceId: String,
        enabled: Int,
        applicationName: String,
    ) -> NotificationFilterMutation?,
    private val forgetFilter: suspend (sourceId: String) -> NotificationFilterMutation?,
    private val applicationName: suspend (sourceId: String) -> String?,
) {
    constructor(
        scope: CoroutineScope,
        notificationFilters: NotificationFilterCoordinator,
        applicationName: suspend (sourceId: String) -> String?,
    ) : this(
        scope = scope,
        setFilter = notificationFilters::setCompatibilityFilter,
        forgetFilter = notificationFilters::forgetCompatibilityFilter,
        applicationName = applicationName,
    )

    private sealed interface Request {
        val sourceId: String
        val result: CompletableDeferred<NotificationFilterMutation?>

        data class Set(
            override val sourceId: String,
            val enabled: Int,
            override val result: CompletableDeferred<NotificationFilterMutation?>,
        ) : Request

        data class Forget(
            override val sourceId: String,
            override val result: CompletableDeferred<NotificationFilterMutation?>,
        ) : Request
    }

    private val logger = Logger.withTag("RockpoolNotificationFilters")
    private val requests = Channel<Request>(Channel.UNLIMITED)

    init {
        scope.launch {
            for (request in requests) {
                execute(request)
            }
        }.invokeOnCompletion { cause ->
            requests.close(cause)
        }
    }

    fun enqueueSet(sourceId: String, enabled: Int): Deferred<NotificationFilterMutation?> =
        enqueue { result -> Request.Set(sourceId, enabled, result) }

    fun enqueueForget(sourceId: String): Deferred<NotificationFilterMutation?> =
        enqueue { result -> Request.Forget(sourceId, result) }

    private fun enqueue(
        create: (CompletableDeferred<NotificationFilterMutation?>) -> Request,
    ): Deferred<NotificationFilterMutation?> {
        val result = CompletableDeferred<NotificationFilterMutation?>()
        val request = create(result)
        if (requests.trySend(request).isFailure) {
            logger.w { "notification filter queue unavailable for ${request.sourceId}" }
            result.complete(null)
        }
        return result
    }

    private suspend fun execute(request: Request) {
        try {
            val result = when (request) {
                is Request.Set -> {
                    val name = applicationName(request.sourceId)?.ifBlank { null }
                        ?: request.sourceId
                    setFilter(request.sourceId, request.enabled, name)
                }
                is Request.Forget -> forgetFilter(request.sourceId)
            }
            if (result == null) {
                logger.w { "notification filter mutation failed for ${request.sourceId}" }
            } else {
                if (!result.runtimePolicyUpdated) {
                    logger.w {
                        "notification provider policy is pending for ${request.sourceId}"
                    }
                }
                if (!result.applicationStateUpdated) {
                    logger.w {
                        "notification application state is pending for ${request.sourceId}"
                    }
                }
            }
            request.result.complete(result)
        } catch (e: CancellationException) {
            request.result.cancel(e)
            throw e
        } catch (e: Exception) {
            logger.w(e) { "notification filter mutation failed for ${request.sourceId}" }
            request.result.complete(null)
        }
    }
}
