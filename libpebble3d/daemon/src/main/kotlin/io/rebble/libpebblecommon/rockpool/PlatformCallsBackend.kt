/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.calls.Call
import io.rebble.libpebblecommon.calls.LegacyPhoneReceiver
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

/** Bridges provider-issued call identities into libpebble3's phone-control flow. */
internal class PlatformCallsBackend(
    private val controller: PlatformProviderController,
    private val commandScope: CoroutineScope =
        CoroutineScope(SupervisorJob() + Dispatchers.IO),
    private val executeCommand: suspend (Int, String, () -> Boolean) -> Int? =
        controller::callCommand,
    private val lookupContactName: suspend (String) -> String? = { null },
) : LegacyPhoneReceiver {
    private data class PendingCommand(
        val command: Int,
        val id: String,
        val generation: Long,
    )

    private class CallerLookup(val number: String)

    private val logger = Logger.withTag("PlatformCallsBackend")
    private val stateLock = Any()
    private val commands = Channel<PendingCommand>(MAX_PENDING_COMMANDS)
    private var target: MutableStateFlow<Call?>? = null
    private var currentId: String? = null
    private var currentState = PlatformProviderController.CALL_ENDED
    private var currentName = ""
    private var currentNumber = ""
    private var callerLookup: CallerLookup? = null
    private var callerLookupCompleted = false
    private var currentGeneration = 0L
    private var currentCookie = 0u
    private var nextCookie = 1u
    private var answerCommandClaimed = false
    private var hangupCommandClaimed = false

    init {
        controller.addListener(::providerSnapshotChanged)
        controller.addCallListener(::providerCall)
        commandScope.launch {
            for (pending in commands) {
                val current = synchronized(stateLock) {
                    commandReservationCurrentLocked(pending)
                }
                if (!current) continue

                val status = try {
                    executeCommand(pending.command, pending.id) {
                        synchronized(stateLock) {
                            commandReservationCurrentLocked(pending)
                        }
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) {
                        "platform call command ${pending.command} for ${pending.id} failed"
                    }
                    synchronized(stateLock) { releaseCommandLocked(pending) }
                    continue
                }
                if (status == null) continue
                if (status != STATUS_OK) {
                    synchronized(stateLock) { releaseCommandLocked(pending) }
                    logger.w {
                        "platform call command ${pending.command} for ${pending.id} failed: $status"
                    }
                }
            }
        }
    }

    override fun init(currentCall: MutableStateFlow<Call?>) {
        synchronized(stateLock) {
            check(target == null || target === currentCall) {
                "platform call receiver initialized more than once"
            }
            target = currentCall
            publishCurrentCallLocked()
        }
    }

    internal fun providerSnapshotChanged(snapshot: PlatformProviderSnapshot) {
        if (snapshot.domains and CALLS_DOMAIN != 0L) return
        synchronized(stateLock) {
            if (currentId == null) return
            clearLocked()
            publishCurrentCallLocked()
        }
    }

    internal fun providerCall(event: PlatformCallEvent) {
        val lookup = synchronized(stateLock) {
            if (event.state == PlatformProviderController.CALL_ENDED) {
                if (event.id != currentId) return
                clearLocked()
                publishCurrentCallLocked()
                return
            }
            if (event.id != currentId || event.number != currentNumber) {
                currentName = ""
                callerLookup = null
                callerLookupCompleted = false
            }
            if (event.id != currentId) {
                currentGeneration++
                currentId = event.id
                currentCookie = allocateCookieLocked()
                answerCommandClaimed = false
                hangupCommandClaimed = false
            } else if (event.state == PlatformProviderController.CALL_RINGING &&
                currentState != PlatformProviderController.CALL_RINGING
            ) {
                answerCommandClaimed = false
            }
            currentState = event.state
            currentNumber = event.number
            if (event.name.isNotBlank()) {
                currentName = event.name
                callerLookup = null
                callerLookupCompleted = true
            }
            val pending = if (currentName.isEmpty() && currentNumber.isNotBlank() &&
                callerLookup == null && !callerLookupCompleted
            ) {
                CallerLookup(currentNumber).also { callerLookup = it }
            } else {
                null
            }
            publishCurrentCallLocked()
            pending
        } ?: return

        commandScope.launch {
            val name = try {
                withTimeoutOrNull(CALLER_LOOKUP_TIMEOUT_MS) {
                    lookupContactName(lookup.number)
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.w(e) { "platform caller-name lookup failed" }
                null
            }
            synchronized(stateLock) {
                // Identity also rejects ended/replaced calls and number changes,
                // including a number changing away and back during a lookup.
                if (callerLookup !== lookup) return@synchronized
                callerLookup = null
                callerLookupCompleted = true
                if (!name.isNullOrBlank()) currentName = name
                publishCurrentCallLocked()
            }
        }
    }

    private fun publishCurrentCallLocked() {
        // Pebble ignores repeated IncomingCall packets while a call is showing.
        // Resolve the name before the first ringing event, with a bounded wait.
        // Answered/ended calls must still propagate while the lookup is pending.
        target?.value = if (currentState == PlatformProviderController.CALL_RINGING &&
            callerLookup != null
        ) null else currentCallLocked()
    }

    private fun currentCallLocked(): Call? {
        val id = currentId ?: return null
        val generation = currentGeneration
        val cookie = currentCookie
        val contactName = currentName.ifEmpty { null }
        return when (currentState) {
            PlatformProviderController.CALL_RINGING -> Call.RingingCall(
                contactName = contactName,
                contactNumber = currentNumber,
                cookie = cookie,
                onCallEnd = {
                    submitCommand(
                        PlatformProviderController.CALL_HANG_UP,
                        id,
                        generation,
                    )
                },
                onCallAnswer = {
                    submitCommand(
                        PlatformProviderController.CALL_ANSWER,
                        id,
                        generation,
                    )
                },
            )
            PlatformProviderController.CALL_DIALING -> Call.DialingCall(
                contactName = contactName,
                contactNumber = currentNumber,
                cookie = cookie,
                onCallEnd = {
                    submitCommand(
                        PlatformProviderController.CALL_HANG_UP,
                        id,
                        generation,
                    )
                },
            )
            PlatformProviderController.CALL_ACTIVE -> Call.ActiveCall(
                contactName = contactName,
                contactNumber = currentNumber,
                cookie = cookie,
                onCallEnd = {
                    submitCommand(
                        PlatformProviderController.CALL_HANG_UP,
                        id,
                        generation,
                    )
                },
            )
            PlatformProviderController.CALL_HELD -> Call.HoldingCall(
                contactName = contactName,
                contactNumber = currentNumber,
                cookie = cookie,
                onCallEnd = {
                    submitCommand(
                        PlatformProviderController.CALL_HANG_UP,
                        id,
                        generation,
                    )
                },
            )
            else -> null
        }
    }

    private fun submitCommand(command: Int, id: String, generation: Long) {
        val pending = synchronized(stateLock) {
            if (commandAllowedLocked(command, id, generation)) {
                PendingCommand(command, id, generation).also(::reserveCommandLocked)
            } else {
                null
            }
        } ?: return
        if (commands.trySend(pending).isFailure) {
            synchronized(stateLock) { releaseCommandLocked(pending) }
            logger.w { "platform call command queue is full; dropping $command for $id" }
        }
    }

    private fun commandAllowedLocked(command: Int, id: String, generation: Long): Boolean =
        currentId == id && currentGeneration == generation &&
            currentState != PlatformProviderController.CALL_ENDED &&
            when (command) {
                PlatformProviderController.CALL_ANSWER ->
                    currentState == PlatformProviderController.CALL_RINGING &&
                        !answerCommandClaimed && !hangupCommandClaimed
                PlatformProviderController.CALL_HANG_UP -> !hangupCommandClaimed
                else -> false
            }

    private fun reserveCommandLocked(pending: PendingCommand) {
        when (pending.command) {
            PlatformProviderController.CALL_ANSWER -> answerCommandClaimed = true
            PlatformProviderController.CALL_HANG_UP -> hangupCommandClaimed = true
        }
    }

    private fun commandReservationCurrentLocked(pending: PendingCommand): Boolean =
        currentId == pending.id && currentGeneration == pending.generation &&
            currentState != PlatformProviderController.CALL_ENDED &&
            when (pending.command) {
                PlatformProviderController.CALL_ANSWER ->
                    currentState == PlatformProviderController.CALL_RINGING &&
                        answerCommandClaimed
                PlatformProviderController.CALL_HANG_UP -> hangupCommandClaimed
                else -> false
            }

    private fun releaseCommandLocked(pending: PendingCommand) {
        if (currentId != pending.id || currentGeneration != pending.generation) return
        when (pending.command) {
            PlatformProviderController.CALL_ANSWER -> answerCommandClaimed = false
            PlatformProviderController.CALL_HANG_UP -> hangupCommandClaimed = false
        }
    }

    private fun clearLocked() {
        currentGeneration++
        currentId = null
        currentState = PlatformProviderController.CALL_ENDED
        currentName = ""
        currentNumber = ""
        callerLookup = null
        callerLookupCompleted = false
        currentCookie = 0u
        answerCommandClaimed = false
        hangupCommandClaimed = false
    }

    private fun allocateCookieLocked(): UInt {
        val allocated = nextCookie
        nextCookie = if (nextCookie == UInt.MAX_VALUE) 1u else nextCookie + 1u
        return allocated
    }

    private companion object {
        private const val CALLS_DOMAIN = 1L shl 3
        private const val STATUS_OK = 0
        private const val MAX_PENDING_COMMANDS = 8
        private const val CALLER_LOOKUP_TIMEOUT_MS = 1_000L
    }
}
