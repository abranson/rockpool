/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.calls.Call
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.util.ArrayDeque
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class PlatformCallsBackendTest {
    private data class Command(val command: Int, val id: String)

    @Test
    fun mapsLifecycleAndPreservesCookieAcrossOneCall() {
        val commands = mutableListOf<Command>()
        val backend = backend(commands)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)

        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_RINGING,
                "call_a",
                "Alice",
                "+123",
            )
        )
        val ringing = assertIs<Call.RingingCall>(current.value)
        assertEquals("Alice", ringing.contactName)
        assertEquals("+123", ringing.contactNumber)
        assertTrue(ringing.cookie != 0u)

        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_ACTIVE,
                "call_a",
                "Alice",
                "+123",
            )
        )
        val active = assertIs<Call.ActiveCall>(current.value)
        assertEquals(ringing.cookie, active.cookie)

        ringing.answerCall()
        assertTrue(commands.isEmpty())
        ringing.endCall()
        active.endCall()
        assertEquals(
            listOf(
                Command(PlatformProviderController.CALL_HANG_UP, "call_a"),
            ),
            commands,
        )

        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_ENDED,
                "call_a",
                "",
                "",
            )
        )
        assertNull(current.value)
    }

    @Test
    fun staleEventsAndCallbacksCannotAffectReplacementCall() {
        val commands = mutableListOf<Command>()
        val backend = backend(commands)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)

        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_RINGING,
                "call_a",
                "Alice",
                "1",
            )
        )
        val old = assertIs<Call.RingingCall>(current.value)
        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_RINGING,
                "call_b",
                "Bob",
                "2",
            )
        )
        val replacement = assertIs<Call.RingingCall>(current.value)
        assertTrue(old.cookie != replacement.cookie)

        old.answerCall()
        old.endCall()
        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_ENDED,
                "call_a",
                "",
                "",
            )
        )
        assertEquals(replacement, current.value)
        assertTrue(commands.isEmpty())

        replacement.answerCall()
        replacement.endCall()
        assertEquals(
            listOf(
                Command(PlatformProviderController.CALL_ANSWER, "call_b"),
                Command(PlatformProviderController.CALL_HANG_UP, "call_b"),
            ),
            commands,
        )
    }

    @Test
    fun providerDomainLossClearsCurrentCall() {
        val backend = backend(mutableListOf())
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_ACTIVE,
                "call_a",
                "Alice",
                "1",
            )
        )
        assertIs<Call.ActiveCall>(current.value)

        backend.providerSnapshotChanged(
            PlatformProviderSnapshot(
                state = "degraded",
                domains = 0,
                supportedDomains = 1L shl 3,
                degradedDomains = 1L shl 3,
            )
        )

        assertNull(current.value)
    }

    @Test
    fun resolvesMissingCallerNameWithoutDelayingTheInitialCall() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val lookupStarted = CompletableDeferred<Unit>()
            val releaseLookup = CompletableDeferred<Unit>()
            val backend = PlatformCallsBackend(
                controller = PlatformProviderController(),
                commandScope = scope,
                lookupContactName = { number ->
                    assertEquals("+123", number)
                    lookupStarted.complete(Unit)
                    releaseLookup.await()
                    "Alice"
                },
            )
            val current = MutableStateFlow<Call?>(null)
            backend.init(current)

            backend.providerCall(
                PlatformCallEvent(
                    PlatformProviderController.CALL_RINGING,
                    "call_a",
                    "",
                    "+123",
                )
            )
            withTimeout(1_000) { lookupStarted.await() }
            val unresolved = assertIs<Call.RingingCall>(current.value)
            assertNull(unresolved.contactName)
            assertEquals("+123", unresolved.contactNumber)

            releaseLookup.complete(Unit)
            val resolved = withTimeout(1_000) {
                current.filterNotNull().first { it.contactName == "Alice" }
            }
            assertEquals(unresolved.cookie, resolved.cookie)
            assertEquals("+123", resolved.contactNumber)
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun resetBoundaryDropsOldCallBeforeReplacementGeneration() {
        val commands = mutableListOf<Command>()
        val backend = backend(commands)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_RINGING,
                "old_call",
                "Alice",
                "1",
            )
        )
        val old = assertIs<Call.RingingCall>(current.value)

        // This is the unavailable snapshot emitted for the native reset
        // marker, before replacement-helper events are dispatched.
        backend.providerSnapshotChanged(
            PlatformProviderSnapshot(
                state = "degraded",
                domains = 0,
                supportedDomains = 1L shl 3,
                degradedDomains = 1L shl 3,
            )
        )
        assertNull(current.value)

        backend.providerCall(
            PlatformCallEvent(
                PlatformProviderController.CALL_RINGING,
                "new_call",
                "Bob",
                "2",
            )
        )
        val replacement = assertIs<Call.RingingCall>(current.value)
        assertEquals("Bob", replacement.contactName)
        assertTrue(old.cookie != replacement.cookie)

        // A callback captured from the discarded generation remains inert.
        old.answerCall()
        old.endCall()
        assertTrue(commands.isEmpty())
    }

    @Test
    fun queuedAnswerIsDroppedWhenCallEndsBeforeDispatcherRelease() {
        val commands = mutableListOf<Command>()
        val dispatcher = QueuedDispatcher()
        val backend = backend(commands, dispatcher)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(ringingEvent("call_a"))

        assertIs<Call.RingingCall>(current.value).answerCall()
        backend.providerCall(endedEvent("call_a"))
        dispatcher.release()

        assertTrue(commands.isEmpty())
    }

    @Test
    fun queuedHangupIsDroppedWhenReplacementCallArrivesBeforeDispatcherRelease() {
        val commands = mutableListOf<Command>()
        val dispatcher = QueuedDispatcher()
        val backend = backend(commands, dispatcher)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(ringingEvent("call_a"))

        assertIs<Call.RingingCall>(current.value).endCall()
        backend.providerCall(ringingEvent("call_b"))
        dispatcher.release()

        assertTrue(commands.isEmpty())
    }

    @Test
    fun queuedCommandIsDroppedWhenProviderResetsBeforeDispatcherRelease() {
        val commands = mutableListOf<Command>()
        val dispatcher = QueuedDispatcher()
        val backend = backend(commands, dispatcher)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(ringingEvent("call_a"))

        assertIs<Call.RingingCall>(current.value).answerCall()
        backend.providerSnapshotChanged(
            PlatformProviderSnapshot(
                state = "degraded",
                domains = 0,
                supportedDomains = 1L shl 3,
                degradedDomains = 1L shl 3,
            )
        )
        dispatcher.release()

        assertTrue(commands.isEmpty())
    }

    @Test
    fun queuedCurrentCallCommandExecutesWhenDispatcherReleases() {
        val commands = mutableListOf<Command>()
        val dispatcher = QueuedDispatcher()
        val backend = backend(commands, dispatcher)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(ringingEvent("call_a"))

        assertIs<Call.RingingCall>(current.value).answerCall()
        dispatcher.release()

        assertEquals(listOf(Command(PlatformProviderController.CALL_ANSWER, "call_a")), commands)
    }

    @Test
    fun `answer then hangup executes in FIFO order`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val firstStarted = CompletableDeferred<Unit>()
            val releaseFirst = CompletableDeferred<Unit>()
            val secondCompleted = CompletableDeferred<Unit>()
            val commands = mutableListOf<Command>()
            val backend = backendWithExecutor(scope) { command, id ->
                commands += Command(command, id)
                if (commands.size == 1) {
                    firstStarted.complete(Unit)
                    releaseFirst.await()
                } else {
                    secondCompleted.complete(Unit)
                }
                0
            }
            val current = MutableStateFlow<Call?>(null)
            backend.init(current)
            backend.providerCall(ringingEvent("call_a"))
            val ringing = assertIs<Call.RingingCall>(current.value)

            ringing.answerCall()
            withTimeout(1_000) { firstStarted.await() }
            ringing.endCall()
            assertEquals(
                listOf(Command(PlatformProviderController.CALL_ANSWER, "call_a")),
                commands,
            )

            releaseFirst.complete(Unit)
            withTimeout(1_000) { secondCompleted.await() }
            assertEquals(
                listOf(
                    Command(PlatformProviderController.CALL_ANSWER, "call_a"),
                    Command(PlatformProviderController.CALL_HANG_UP, "call_a"),
                ),
                commands,
            )
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `hangup suppresses later answer before its command finishes`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val commandStarted = CompletableDeferred<Unit>()
            val releaseCommand = CompletableDeferred<Unit>()
            val commands = mutableListOf<Command>()
            val backend = backendWithExecutor(scope) { command, id ->
                commands += Command(command, id)
                commandStarted.complete(Unit)
                releaseCommand.await()
                0
            }
            val current = MutableStateFlow<Call?>(null)
            backend.init(current)
            backend.providerCall(ringingEvent("call_a"))
            val ringing = assertIs<Call.RingingCall>(current.value)

            ringing.endCall()
            withTimeout(1_000) { commandStarted.await() }
            ringing.answerCall()
            assertEquals(
                listOf(Command(PlatformProviderController.CALL_HANG_UP, "call_a")),
                commands,
            )

            releaseCommand.complete(Unit)
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `duplicate answer and hangup callbacks are suppressed`() {
        val answerCommands = mutableListOf<Command>()
        val answerBackend = backend(answerCommands)
        val answerCurrent = MutableStateFlow<Call?>(null)
        answerBackend.init(answerCurrent)
        answerBackend.providerCall(ringingEvent("answer_call"))
        val answerCall = assertIs<Call.RingingCall>(answerCurrent.value)

        answerCall.answerCall()
        answerCall.answerCall()
        assertEquals(
            listOf(Command(PlatformProviderController.CALL_ANSWER, "answer_call")),
            answerCommands,
        )

        val hangupCommands = mutableListOf<Command>()
        val hangupBackend = backend(hangupCommands)
        val hangupCurrent = MutableStateFlow<Call?>(null)
        hangupBackend.init(hangupCurrent)
        hangupBackend.providerCall(ringingEvent("hangup_call"))
        val hangupCall = assertIs<Call.RingingCall>(hangupCurrent.value)

        hangupCall.endCall()
        hangupCall.endCall()
        assertEquals(
            listOf(Command(PlatformProviderController.CALL_HANG_UP, "hangup_call")),
            hangupCommands,
        )
    }

    @Test
    fun `duplicate queued answers cannot crowd out hangup`() {
        val commands = mutableListOf<Command>()
        val dispatcher = QueuedDispatcher()
        val backend = backend(commands, dispatcher)
        val current = MutableStateFlow<Call?>(null)
        backend.init(current)
        backend.providerCall(ringingEvent("call_a"))
        val ringing = assertIs<Call.RingingCall>(current.value)

        repeat(16) { ringing.answerCall() }
        ringing.endCall()
        dispatcher.release()

        assertEquals(
            listOf(
                Command(PlatformProviderController.CALL_ANSWER, "call_a"),
                Command(PlatformProviderController.CALL_HANG_UP, "call_a"),
            ),
            commands,
        )
    }

    @Test
    fun `provider reset after dequeue rejects stale call command`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val commandDequeued = CompletableDeferred<Unit>()
            val releaseGate = CompletableDeferred<Unit>()
            val commandFinished = CompletableDeferred<Unit>()
            val commands = mutableListOf<Command>()
            val backend = PlatformCallsBackend(
                controller = PlatformProviderController(),
                commandScope = scope,
                executeCommand = { command, id, isCurrent ->
                    commandDequeued.complete(Unit)
                    releaseGate.await()
                    val status = if (isCurrent()) {
                        commands += Command(command, id)
                        0
                    } else {
                        null
                    }
                    commandFinished.complete(Unit)
                    status
                },
            )
            val current = MutableStateFlow<Call?>(null)
            backend.init(current)
            backend.providerCall(ringingEvent("same_id"))

            assertIs<Call.RingingCall>(current.value).answerCall()
            withTimeout(1_000) { commandDequeued.await() }
            backend.providerSnapshotChanged(
                PlatformProviderSnapshot(
                    state = "degraded",
                    domains = 0,
                    supportedDomains = 1L shl 3,
                    degradedDomains = 1L shl 3,
                ),
            )
            backend.providerCall(ringingEvent("same_id"))
            releaseGate.complete(Unit)

            withTimeout(1_000) { commandFinished.await() }
            assertTrue(commands.isEmpty())
        } finally {
            scope.cancel()
        }
        Unit
    }

    @Test
    fun `failed hangup releases its claim for a later answer`() = runBlocking {
        val commands = mutableListOf<Command>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        try {
            val backend = backendWithExecutor(scope) { command, id ->
                commands += Command(command, id)
                if (commands.size == 1) -1 else 0
            }
            val current = MutableStateFlow<Call?>(null)
            backend.init(current)
            backend.providerCall(ringingEvent("call_a"))
            val ringing = assertIs<Call.RingingCall>(current.value)

            ringing.endCall()
            ringing.answerCall()

            assertEquals(
                listOf(
                    Command(PlatformProviderController.CALL_HANG_UP, "call_a"),
                    Command(PlatformProviderController.CALL_ANSWER, "call_a"),
                ),
                commands,
            )
        } finally {
            scope.cancel()
        }
        Unit
    }

    private fun ringingEvent(id: String) = PlatformCallEvent(
        PlatformProviderController.CALL_RINGING,
        id,
        "Alice",
        "1",
    )

    private fun endedEvent(id: String) = PlatformCallEvent(
        PlatformProviderController.CALL_ENDED,
        id,
        "",
        "",
    )

    private fun backend(
        commands: MutableList<Command>,
        dispatcher: CoroutineDispatcher = Dispatchers.Unconfined,
    ) = PlatformCallsBackend(
        controller = PlatformProviderController(),
        commandScope = CoroutineScope(SupervisorJob() + dispatcher),
        executeCommand = { command, id, isCurrent ->
            if (isCurrent()) {
                commands += Command(command, id)
                0
            } else {
                null
            }
        },
    )

    private fun backendWithExecutor(
        scope: CoroutineScope,
        executeCommand: suspend (Int, String) -> Int,
    ) = PlatformCallsBackend(
        controller = PlatformProviderController(),
        commandScope = scope,
        executeCommand = { command, id, isCurrent ->
            if (isCurrent()) executeCommand(command, id) else null
        },
    )

    private class QueuedDispatcher : CoroutineDispatcher() {
        private val queued = ArrayDeque<Runnable>()

        override fun dispatch(context: kotlin.coroutines.CoroutineContext, block: Runnable) {
            queued.addLast(block)
        }

        fun release() {
            while (queued.isNotEmpty()) {
                queued.removeFirst().run()
            }
        }
    }
}
