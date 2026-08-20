/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.linux.notifications.LinuxNotificationEvent
import io.rebble.libpebblecommon.linux.notifications.LinuxNotificationCommand
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class PlatformProviderControllerTest {
    private val controller = PlatformProviderController()

    @Test
    fun lateMediaListenerReceivesCachedInitialVolumeAndLoss() {
        controller.providerMediaVolume(37)
        val received = mutableListOf<Int?>()

        controller.addMediaListener(received::add)
        controller.providerSnapshotChanged(
            PlatformProviderSnapshot(
                state = "degraded",
                domains = 0,
                supportedDomains = PlatformProviderController.MEDIA_DOMAIN,
                degradedDomains = PlatformProviderController.MEDIA_DOMAIN,
            )
        )

        assertEquals(listOf(37, null), received)
    }

    @Test
    fun decodesOnlyStrictSystemVolumeFields() {
        assertEquals(0, controller.decodeMedia(listOf(1, 0)))
        assertEquals(100, controller.decodeMedia(listOf(1, 100)))
        assertNull(controller.decodeMedia(listOf(0, 50)))
        assertNull(controller.decodeMedia(listOf(1, -1)))
        assertNull(controller.decodeMedia(listOf(1, 101)))
        assertNull(controller.decodeMedia(listOf(1, 50, 0)))
    }

    @Test
    fun decodesPostedAndClosedNotifications() {
        val posted = assertIs<PlatformNotificationEvent.Posted>(
            controller.decodeNotification(
                record(
                    type = 2,
                    flags = 1,
                    timestampMs = 1234,
                    strings = arrayOf(
                        "42", "41", "org.example.mail", "Mail", "Subject", "Body",
                        "email.arrived", "icon-lock-email",
                    ),
                )
            )
        )
        assertEquals("42", posted.id)
        assertEquals("41", posted.replacesId)
        assertEquals("org.example.mail", posted.applicationId)
        assertEquals(1234, posted.timestampMs)

        val closed = assertIs<PlatformNotificationEvent.Closed>(
            controller.decodeNotification(
                record(
                    type = 3,
                    closeReason = 2,
                    strings = arrayOf("42", "", "", "", "", "", "", ""),
                )
            )
        )
        assertEquals("42", closed.id)
        assertEquals(2, closed.reason)
    }

    @Test
    fun rejectsInvalidIdsAndPostedFields() {
        fun posted(id: String = "1", replacesId: String = "") = record(
            type = 2,
            strings = arrayOf(id, replacesId, "org.example", "Example", "Title", "", "", ""),
        )

        assertNull(controller.decodeNotification(posted(id = "0")))
        assertNull(controller.decodeNotification(posted(id = "-1")))
        assertNull(controller.decodeNotification(posted(id = "4294967296")))
        assertNull(controller.decodeNotification(posted(replacesId = "not-a-number")))
        assertNull(
            controller.decodeNotification(
                record(
                    type = 2,
                    strings = arrayOf("1", "", "", "Example", "Title", "", "", ""),
                )
            )
        )
        assertNull(
            controller.decodeNotification(
                record(
                    type = 2,
                    strings = arrayOf("1", "", "org.example", "Example", "", "", "", ""),
                )
            )
        )
    }

    @Test
    fun rejectsMalformedLengthsTextFlagsAndTrailingData() {
        val valid = record(
            type = 2,
            strings = arrayOf("1", "", "org.example", "Example", "Title", "", "", ""),
        )

        assertNull(controller.decodeNotification(valid.copyOf(valid.size + 1)))
        assertNull(controller.decodeNotification(valid.clone().also { it.putInt(20, 65) }))
        assertNull(controller.decodeNotification(valid.clone().also { it.putInt(28, 257) }))
        assertNull(controller.decodeNotification(valid.clone().also { it.putInt(4, 4) }))
        assertNull(controller.decodeNotification(valid.clone().also { it[52] = 0 }))
        assertNull(controller.decodeNotification(valid.clone().also { it[52] = 0xc3.toByte() }))
    }

    @Test
    fun closedRecordCannotCarryPostedFields() {
        assertNull(
            controller.decodeNotification(
                record(
                    type = 3,
                    closeReason = 5,
                    strings = arrayOf("1", "", "", "", "", "", "", ""),
                )
            )
        )
        assertNull(
            controller.decodeNotification(
                record(
                    type = 3,
                    closeReason = 1,
                    strings = arrayOf("1", "", "org.example", "", "", "", "", ""),
                )
            )
        )
    }

    @Test
    fun decodesCallLifecycleRecords() {
        val ringing = assertIs<PlatformCallEvent>(
            controller.decodeCall(
                callRecord(
                    state = PlatformProviderController.CALL_RINGING,
                    id = "abc_123",
                    name = "Alice",
                    number = "+123456",
                )
            )
        )
        assertEquals("abc_123", ringing.id)
        assertEquals("Alice", ringing.name)
        assertEquals("+123456", ringing.number)

        val ended = assertIs<PlatformCallEvent>(
            controller.decodeCall(
                callRecord(
                    state = PlatformProviderController.CALL_ENDED,
                    id = "abc_123",
                )
            )
        )
        assertEquals(PlatformProviderController.CALL_ENDED, ended.state)
        assertEquals("abc_123", ended.id)
    }

    @Test
    fun rejectsMalformedCallRecords() {
        val valid = callRecord(
            state = PlatformProviderController.CALL_ACTIVE,
            id = "abc_123",
            name = "Alice",
            number = "+123456",
        )

        assertNull(controller.decodeCall(valid.copyOf(valid.size + 1)))
        assertNull(controller.decodeCall(valid.clone().also { it[2] = 1 }))
        assertNull(controller.decodeCall(valid.clone().also { it.putInt(4, 99) }))
        assertNull(controller.decodeCall(valid.clone().also { it.putInt(8, Int.MAX_VALUE) }))
        assertNull(
            controller.decodeCall(
                callRecord(PlatformProviderController.CALL_ACTIVE, "bad-id")
            )
        )
        assertNull(
            controller.decodeCall(
                callRecord(PlatformProviderController.CALL_ENDED, "abc", name = "Alice")
            )
        )
        assertNull(
            controller.decodeCall(
                callRecord(
                    PlatformProviderController.CALL_ACTIVE,
                    "abc",
                    name = "\u0000",
                )
            )
        )
    }

    @Test
    fun parsesPersistedSourceFilters() {
        val filters = notificationFiltersFromEntries(
            mapOf(
                "notifications.mail.source" to "org.example.mail",
                "notifications.mail.enabled" to "0",
                "notifications.mail.name" to "Work mail",
                "notifications.mail.icon" to "icon-lock-email",
                "notifications.chat.source" to "org.example.chat",
                "notifications.chat.enabled" to "2",
                "notifications.conditional.source" to "org.example.conditional",
                "notifications.conditional.enabled" to "1",
                "notifications.legacy.source" to "org.example.legacy",
                "notifications.legacy.enabled" to "false",
            )
        )

        assertEquals(
            PlatformNotificationFilter(false, "Work mail", "icon-lock-email"),
            filters["org.example.mail"],
        )
        assertEquals(
            PlatformNotificationFilter(true, "org.example.chat", ""),
            filters["org.example.chat"],
        )
        assertEquals(
            PlatformNotificationFilter(
                true,
                "org.example.conditional",
                "",
                suppressWhenActive = true,
            ),
            filters["org.example.conditional"],
        )
        assertEquals(
            PlatformNotificationFilter(false, "org.example.legacy", ""),
            filters["org.example.legacy"],
        )
    }

    @Test
    fun sourceFilterSuppressesNewPostsAndClosesReplacements() {
        val disabled = mapOf(
            "org.example.mail" to PlatformNotificationFilter(false, "Mail", ""),
        )
        assertNull(mapPlatformNotificationEvent(postedEvent(), disabled))

        val close = assertIs<LinuxNotificationEvent.Closed>(
            mapPlatformNotificationEvent(postedEvent(replacesId = "40"), disabled)
        )
        assertEquals("40", close.id)
    }

    @Test
    fun conditionalSourceFilterSuppressesOnlyWhileDeviceIsActive() {
        val conditional = mapOf(
            "org.example.mail" to PlatformNotificationFilter(
                true,
                "Mail",
                "",
                suppressWhenActive = true,
            ),
        )

        assertIs<LinuxNotificationEvent.Posted>(
            mapPlatformNotificationEvent(postedEvent(), conditional, deviceActive = false)
        )
        assertNull(
            mapPlatformNotificationEvent(postedEvent(), conditional, deviceActive = true)
        )
        val close = assertIs<LinuxNotificationEvent.Closed>(
            mapPlatformNotificationEvent(
                postedEvent(replacesId = "40"),
                conditional,
                deviceActive = true,
            )
        )
        assertEquals("40", close.id)
    }

    @Test
    fun sourceFilterOverridesNameAndIconBeforePinConstruction() {
        val posted = assertIs<LinuxNotificationEvent.Posted>(
            mapPlatformNotificationEvent(
                postedEvent(),
                mapOf(
                    "org.example.mail" to PlatformNotificationFilter(
                        enabled = true,
                        name = "Work mail",
                        icon = "icon-lock-email",
                    )
                ),
            )
        )
        assertEquals("Work mail", posted.notification.content.appName)
        assertEquals("icon-lock-email", posted.notification.content.iconName)
    }

    @Test
    fun providerReplyFlagMapsToLinuxReplyAuthority() {
        val posted = assertIs<LinuxNotificationEvent.Posted>(
            mapPlatformNotificationEvent(
                postedEvent(flags = PlatformProviderController.NOTIFICATION_HAS_REPLY_ACTION),
                emptyMap(),
            )
        )

        assertTrue(posted.notification.hasReplyAction)
        assertFalse(posted.notification.hasDefaultAction)
    }

    @Test
    fun providerReplyFlagIsStrippedWithoutMessagingDomain() = runBlocking {
        val backend = PlatformNotificationBackend(controller)
        backend.providerSnapshotChanged(readyNotificationSnapshot())
        backend.providerNotification(
            postedEvent(flags = PlatformProviderController.NOTIFICATION_HAS_REPLY_ACTION)
        )

        val posted = assertIs<LinuxNotificationEvent.Posted>(
            withTimeout(1_000) { backend.events().first() }
        )
        assertFalse(posted.notification.hasReplyAction)
    }

    @Test
    fun notificationQueueOverflowCreatesResetBarrier() = runBlocking {
        val backend = PlatformNotificationBackend(controller, eventCapacity = 2)
        backend.providerSnapshotChanged(readyNotificationSnapshot())
        backend.providerNotification(postedEvent(id = "1"))
        backend.providerNotification(postedEvent(id = "2"))
        backend.providerNotification(postedEvent(id = "3"))

        val collected = async { backend.events().take(2).toList() }
        delay(20)
        backend.providerNotification(postedEvent(id = "4"))
        val events = withTimeout(1_000) { collected.await() }

        assertEquals(LinuxNotificationEvent.Reset, events[0])
        assertEquals(
            "4",
            assertIs<LinuxNotificationEvent.Posted>(events[1]).notification.id,
        )
    }

    @Test
    fun filterReplacementBehindResetCompletesAfterConsumer() = runBlocking {
        val backend = PlatformNotificationBackend(controller, eventCapacity = 1)
        backend.providerSnapshotChanged(readyNotificationSnapshot())
        backend.providerNotification(postedEvent(id = "1"))
        backend.providerNotification(postedEvent(id = "2"))
        val replacement = async {
            assertTrue(
                backend.replaceFilters(
                    mapOf(
                        "org.example.mail" to
                            PlatformNotificationFilter(false, "Mail", ""),
                    )
                )
            )
        }
        delay(20)
        assertFalse(replacement.isCompleted)

        val collector = launch { backend.events().collect { } }
        withTimeout(1_000) { replacement.await() }
        collector.cancelAndJoin()
    }

    @Test
    fun filterReplacementWithoutConsumerReportsPendingCleanup() = runBlocking {
        val backend = PlatformNotificationBackend(
            controller,
            filterApplyTimeoutMs = 25,
        )
        backend.providerSnapshotChanged(readyNotificationSnapshot())
        assertFalse(
            backend.replaceFilters(
                mapOf(
                    "org.example.mail" to
                        PlatformNotificationFilter(false, "Mail", ""),
                )
            )
        )
    }

    @Test
    fun cancellingCollectorDuringResetDoesNotWedgeBackend() = runBlocking {
        val backend = PlatformNotificationBackend(controller, eventCapacity = 1)
        backend.providerSnapshotChanged(readyNotificationSnapshot())
        backend.providerNotification(postedEvent(id = "1"))
        backend.providerNotification(postedEvent(id = "2"))

        val resetStarted = CompletableDeferred<Unit>()
        val collector = launch {
            backend.events().collect { event ->
                if (event === LinuxNotificationEvent.Reset) {
                    resetStarted.complete(Unit)
                    awaitCancellation()
                }
            }
        }
        withTimeout(1_000) { resetStarted.await() }
        collector.cancelAndJoin()

        backend.providerNotification(postedEvent(id = "3"))
        val next = withTimeout(1_000) { backend.events().first() }
        assertEquals(
            "3",
            assertIs<LinuxNotificationEvent.Posted>(next).notification.id,
        )
    }

    @Test
    fun providerGenerationBoundaryResetsNotificationsBeforeDomainDrains() = runBlocking {
        val backend = PlatformNotificationBackend(controller)
        backend.providerSnapshotChanged(readyNotificationSnapshot())

        controller.applyProviderGenerationBoundary(
            listOf(
                listOf(
                    PlatformProviderController.PROVIDER_STATUS_EVENT.toLong(),
                    0,
                    0,
                    0,
                ),
            ),
        )

        assertEquals(0, controller.snapshot().domains)
        assertEquals(
            LinuxNotificationEvent.Reset,
            withTimeout(1_000) { backend.events().first() },
        )
    }

    @Test
    fun dismissDequeuedBeforeResetBarrierIsNotForwarded() = runBlocking {
        val dequeued = CompletableDeferred<Unit>()
        val dispatch = CompletableDeferred<Unit>()
        val forwarded = mutableListOf<Pair<Int, String>>()
        val backend = PlatformNotificationBackend(
            controller,
            eventCapacity = 1,
            executeCommand = { command, id, isCurrent ->
                dequeued.complete(Unit)
                dispatch.await()
                if (isCurrent()) {
                    forwarded += command to id
                    0
                } else {
                    null
                }
            },
        )
        backend.providerSnapshotChanged(readyNotificationSnapshot())

        val retired = async { backend.execute("41", LinuxNotificationCommand.Dismiss) }
        withTimeout(1_000) { dequeued.await() }

        // Fill the queue, then force its Reset barrier while the action is
        // between dequeue and the controller/native dispatch.
        backend.providerNotification(postedEvent(id = "1"))
        backend.providerNotification(postedEvent(id = "2"))
        dispatch.complete(Unit)

        assertFalse(withTimeout(1_000) { retired.await() })
        assertTrue(forwarded.isEmpty())
        assertEquals(LinuxNotificationEvent.Reset, withTimeout(1_000) { backend.events().first() })

        assertTrue(backend.execute("42", LinuxNotificationCommand.Dismiss))
        assertEquals(
            listOf(PlatformProviderController.NOTIFICATION_DISMISS to "42"),
            forwarded,
        )
    }

    @Test
    fun dismissDequeuedBeforeDomainLossIsNotForwardedToReplacement() = runBlocking {
        val dequeued = CompletableDeferred<Unit>()
        val dispatch = CompletableDeferred<Unit>()
        val forwarded = mutableListOf<Pair<Int, String>>()
        val backend = PlatformNotificationBackend(
            controller,
            executeCommand = { command, id, isCurrent ->
                dequeued.complete(Unit)
                dispatch.await()
                if (isCurrent()) {
                    forwarded += command to id
                    0
                } else {
                    null
                }
            },
        )
        backend.providerSnapshotChanged(readyNotificationSnapshot())

        val retired = async { backend.execute("41", LinuxNotificationCommand.Dismiss) }
        withTimeout(1_000) { dequeued.await() }

        backend.providerSnapshotChanged(readyNotificationSnapshot().copy(domains = 0))
        backend.providerSnapshotChanged(readyNotificationSnapshot())
        dispatch.complete(Unit)

        assertFalse(withTimeout(1_000) { retired.await() })
        assertTrue(forwarded.isEmpty())
        assertEquals(LinuxNotificationEvent.Reset, withTimeout(1_000) { backend.events().first() })

        assertTrue(backend.execute("42", LinuxNotificationCommand.Dismiss))
        assertEquals(
            listOf(PlatformProviderController.NOTIFICATION_DISMISS to "42"),
            forwarded,
        )
    }

    @Test
    fun replyDequeuedBeforeResetBarrierIsNotForwarded() = runBlocking {
        val dequeued = CompletableDeferred<Unit>()
        val dispatch = CompletableDeferred<Unit>()
        val forwarded = mutableListOf<Pair<String, String>>()
        val backend = PlatformNotificationBackend(
            controller,
            eventCapacity = 1,
            executeReply = { id, text, isCurrent ->
                dequeued.complete(Unit)
                dispatch.await()
                if (isCurrent()) {
                    forwarded += id to text
                    0
                } else {
                    null
                }
            },
        )
        backend.providerSnapshotChanged(readyMessagingSnapshot())

        val retired = async {
            backend.execute("41", LinuxNotificationCommand.Reply("On my way"))
        }
        withTimeout(1_000) { dequeued.await() }
        backend.providerNotification(postedEvent(id = "1"))
        backend.providerNotification(postedEvent(id = "2"))
        dispatch.complete(Unit)

        assertFalse(withTimeout(1_000) { retired.await() })
        assertTrue(forwarded.isEmpty())
        assertEquals(LinuxNotificationEvent.Reset, withTimeout(1_000) {
            backend.events().first()
        })
    }

    @Test
    fun replyDequeuedBeforeMessagingDomainLossIsNotForwardedToReplacement() = runBlocking {
        val dequeued = CompletableDeferred<Unit>()
        val dispatch = CompletableDeferred<Unit>()
        val forwarded = mutableListOf<Pair<String, String>>()
        val backend = PlatformNotificationBackend(
            controller,
            executeReply = { id, text, isCurrent ->
                dequeued.complete(Unit)
                dispatch.await()
                if (isCurrent()) {
                    forwarded += id to text
                    0
                } else {
                    null
                }
            },
        )
        backend.providerSnapshotChanged(readyMessagingSnapshot())

        val retired = async {
            backend.execute("41", LinuxNotificationCommand.Reply("On my way"))
        }
        withTimeout(1_000) { dequeued.await() }
        backend.providerSnapshotChanged(
            readyMessagingSnapshot().copy(
                domains = PlatformProviderController.NOTIFICATION_DOMAIN,
            )
        )
        backend.providerSnapshotChanged(readyMessagingSnapshot())
        dispatch.complete(Unit)

        assertFalse(withTimeout(1_000) { retired.await() })
        assertTrue(forwarded.isEmpty())
        assertEquals(
            LinuxNotificationEvent.Reset,
            withTimeout(1_000) { backend.events().first() },
        )

        assertTrue(backend.execute("42", LinuxNotificationCommand.Reply("Later")))
        assertEquals(listOf("42" to "Later"), forwarded)
    }

    @Test
    fun failedStatusClearsReadyDomainsAndFailsSupportedDomains() {
        val failed = failedProviderStatusSnapshot(
            PlatformProviderSnapshot(
                state = "ready",
                domains = 3,
                supportedDomains = 7,
                degradedDomains = 4,
                helperPid = 123,
            )
        )

        assertEquals("failed", failed.state)
        assertEquals(0L, failed.domains)
        assertEquals(0L, failed.degradedDomains)
        assertEquals(7L, failed.failedDomains)
        assertEquals(0L, failed.helperPid)
    }

    @Test
    fun restartOperationalRequiresReadyOrClassifiedLiveHelper() {
        assertTrue(PlatformProviderSnapshot(state = "ready").isRestartOperational())
        assertTrue(
            PlatformProviderSnapshot(
                state = "degraded",
                helperPid = 42,
                degradedDomains = 1,
            ).isRestartOperational()
        )
        assertFalse(
            PlatformProviderSnapshot(
                state = "degraded",
                helperPid = 0,
                degradedDomains = 1,
            ).isRestartOperational()
        )
        assertFalse(
            PlatformProviderSnapshot(state = "failed", helperPid = 42)
                .isRestartOperational()
        )
    }

    @Test
    fun restartWaitsThroughBootstrapUntilHelperIsOperational() = runBlocking {
        var polls = 0
        val result = awaitPlatformProviderRestart(
            initial = PlatformProviderSnapshot(state = "degraded", helperPid = 0),
            timeout = 500.milliseconds,
            pollInterval = 1.milliseconds,
        ) {
            polls++
            if (polls == 1) {
                PlatformProviderSnapshot(state = "degraded", helperPid = 0)
            } else {
                PlatformProviderSnapshot(state = "ready", helperPid = 42)
            }
        }

        assertEquals("ready", result.state)
        assertEquals(42L, result.helperPid)
        assertEquals(2, polls)
    }

    @Test
    fun restartReturnsTerminalFailureWithoutPolling() = runBlocking {
        var polled = false
        val failed = PlatformProviderSnapshot(state = "failed")
        val result = awaitPlatformProviderRestart(
            initial = failed,
            timeout = 500.milliseconds,
            pollInterval = 1.milliseconds,
        ) {
            polled = true
            PlatformProviderSnapshot(state = "ready", helperPid = 42)
        }

        assertEquals(failed, result)
        assertFalse(polled)
    }

    @Test
    fun restartTimeoutReturnsLastBootstrapSnapshot() = runBlocking {
        val initial = PlatformProviderSnapshot(state = "degraded", helperPid = 0)
        val result = awaitPlatformProviderRestart(
            initial = initial,
            timeout = 0.milliseconds,
            pollInterval = 1.milliseconds,
        ) {
            PlatformProviderSnapshot(state = "ready", helperPid = 42)
        }

        assertEquals(initial, result)
    }

    @Test
    fun cancelledRestartDoesNotTouchNativeState() = runBlocking {
        var restarted = false

        val result = runCommittedProviderRestart(beginCommit = { false }) {
            restarted = true
            "restarted"
        }

        assertEquals(null, result)
        assertFalse(restarted)
    }

    @Test
    fun committedRestartReturnsItsActualResult() = runBlocking {
        var restarted = false

        val result = runCommittedProviderRestart(beginCommit = { true }) {
            restarted = true
            "restarted"
        }

        assertEquals("restarted", result)
        assertTrue(restarted)
    }

    @Test
    fun decodesStrictLocationSuccessAndErrorRecords() {
        val location = assertIs<PlatformLocationQueryResult.Success>(
            controller.decodeLocation(listOf(7, 0, 123_456_789, -987_654_321, 42, 1_234)),
        ).location
        assertEquals(123_456_789, location.latitudeE7)
        assertEquals(-987_654_321, location.longitudeE7)
        assertEquals(42, location.accuracyM)
        assertEquals(1_234L, location.timestampMs)
        assertEquals(
            PlatformLocationQueryResult.Error(PlatformProviderController.STATUS_BUSY),
            controller.decodeLocation(listOf(7, PlatformProviderController.STATUS_BUSY.toLong(), 0, 0, 0, 0)),
        )
        assertNull(controller.decodeLocation(listOf(0, 0, 0, 0, 0, 0)))
        assertNull(controller.decodeLocation(listOf(7, 0, 900_000_001, 0, 0, 1)))
        assertNull(controller.decodeLocation(listOf(7, 0, 0, 0, -1, 1)))
        assertNull(controller.decodeLocation(listOf(7, 0, 0, 0, 0, 0)))
        assertNull(controller.decodeLocation(listOf(7, 4, 1, 0, 0, 0)))
        assertNull(controller.decodeLocation(listOf(7, 0, 0, 0, 0)))
    }

    @Test
    fun queryCorrelatesOnlyMatchingLocationCompletion() = runBlocking {
        val started = CompletableDeferred<Pair<Int, Int>>()
        val controller = locationController(
            start = { accuracy, timeout ->
                started.complete(accuracy to timeout)
                longArrayOf(0, 42)
            },
        )
        val request = async { controller.queryLocation(highAccuracy = true, timeout = 40.seconds) }

        assertEquals(PlatformProviderController.LOCATION_FINE to 30_000, started.await())
        controller.providerLocationEvents(longArrayOf(41, 0, 1, 2, 3, 4))
        assertFalse(request.isCompleted)
        controller.providerLocationEvents(longArrayOf(42, 0, 1, 2, 3, 4))
        assertEquals(
            PlatformLocationQueryResult.Success(PlatformLocation(1, 2, 3, 4)),
            withTimeout(1_000) { request.await() },
        )
    }

    @Test
    fun queryRejectsMalformedStartAndCancelsOnTimeout() = runBlocking {
        var cancelled = -1L
        val malformed = locationController(start = { _, _ -> longArrayOf(0) })
        assertEquals(
            PlatformLocationQueryResult.Error(PlatformProviderController.STATUS_PROTOCOL_ERROR),
            malformed.queryLocation(false, 1.milliseconds),
        )

        val timedOut = locationController(
            start = { _, _ -> longArrayOf(0, 9) },
            cancel = { id -> cancelled = id; 0 },
        )
        assertEquals(
            PlatformLocationQueryResult.Error(PlatformProviderController.STATUS_UNAVAILABLE),
            withTimeout(2.seconds) { timedOut.queryLocation(false, 1.milliseconds) },
        )
        assertEquals(9L, cancelled)
    }

    @Test
    fun domainLossAndGenerationRetirePendingLocations() = runBlocking {
        suspend fun pending(controller: PlatformProviderController): kotlinx.coroutines.Deferred<PlatformLocationQueryResult> {
            val request = async { controller.queryLocation(false, 5.seconds) }
            delay(1)
            return request
        }

        val lost = locationController(start = { _, _ -> longArrayOf(0, 1) })
        val lostRequest = pending(lost)
        lost.providerSnapshotChanged(locationSnapshot(domains = 0))
        assertEquals(
            PlatformLocationQueryResult.Error(PlatformProviderController.STATUS_UNAVAILABLE),
            withTimeout(1_000) { lostRequest.await() },
        )

        val reset = locationController(start = { _, _ -> longArrayOf(0, 2) })
        val resetRequest = pending(reset)
        reset.applyProviderGenerationBoundary(
            listOf(listOf(PlatformProviderController.PROVIDER_STATUS_EVENT.toLong(), 0, 0, 0)),
        )
        assertEquals(
            PlatformLocationQueryResult.Error(PlatformProviderController.STATUS_UNAVAILABLE),
            withTimeout(1_000) { resetRequest.await() },
        )
    }

    private fun locationController(
        start: (Int, Int) -> LongArray,
        cancel: (Long) -> Int = { 0 },
    ) = PlatformProviderController(
        initialSnapshot = locationSnapshot(),
        locationNativeAvailable = { true },
        locationStartNative = start,
        locationCancelNative = cancel,
    )

    private fun locationSnapshot(domains: Long = PlatformProviderController.LOCATION_DOMAIN) =
        PlatformProviderSnapshot(
            state = "ready",
            domains = domains,
            supportedDomains = PlatformProviderController.LOCATION_DOMAIN,
        )

    private fun record(
        type: Int,
        flags: Int = 0,
        timestampMs: Long = 0,
        closeReason: Int = 0,
        strings: Array<String>,
    ): ByteArray {
        require(strings.size == 8)
        val encoded = strings.map(String::encodeToByteArray)
        return ByteBuffer.allocate(52 + encoded.sumOf(ByteArray::size))
            .order(ByteOrder.LITTLE_ENDIAN)
            .apply {
                putShort(type.toShort())
                putShort(0)
                putInt(flags)
                putLong(timestampMs)
                putInt(closeReason)
                encoded.forEach { putInt(it.size) }
                encoded.forEach(::put)
            }
            .array()
    }

    private fun callRecord(
        state: Int,
        id: String,
        name: String = "",
        number: String = "",
    ): ByteArray {
        val encoded = listOf(id, name, number).map(String::encodeToByteArray)
        return ByteBuffer.allocate(20 + encoded.sumOf(ByteArray::size))
            .order(ByteOrder.LITTLE_ENDIAN)
            .apply {
                putShort(PlatformProviderController.CALL_EVENT.toShort())
                putShort(0)
                putInt(state)
                encoded.forEach { putInt(it.size) }
                encoded.forEach(::put)
            }
            .array()
    }

    private fun postedEvent(
        id: String = "41",
        replacesId: String = "",
        flags: Int = 0,
    ) = PlatformNotificationEvent.Posted(
        flags = flags,
        timestampMs = 0,
        id = id,
        replacesId = replacesId,
        applicationId = "org.example.mail",
        applicationName = "Mail",
        title = "Subject",
        body = "Body",
        category = "email.arrived",
        iconName = "mail-unread",
    )

    private fun readyNotificationSnapshot() = PlatformProviderSnapshot(
        state = "ready",
        domains = 1,
        supportedDomains = 1,
    )

    private fun readyMessagingSnapshot() = PlatformProviderSnapshot(
        state = "ready",
        domains = PlatformProviderController.NOTIFICATION_DOMAIN or
            PlatformProviderController.MESSAGING_DOMAIN,
        supportedDomains = PlatformProviderController.NOTIFICATION_DOMAIN or
            PlatformProviderController.MESSAGING_DOMAIN,
    )

    private fun ByteArray.putInt(offset: Int, value: Int) {
        ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(offset, value)
    }
}
