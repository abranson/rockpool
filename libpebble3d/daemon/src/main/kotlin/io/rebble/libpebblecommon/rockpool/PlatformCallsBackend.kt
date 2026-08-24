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

    private val logger = Logger.withTag("PlatformCallsBackend")
    private val stateLock = Any()
    private val commands = Channel<PendingCommand>(MAX_PENDING_COMMANDS)
    private var target: MutableStateFlow<Call?>? = null
    private var currentId: String? = null
    private var currentState = PlatformProviderController.CALL_ENDED
    private var currentName = ""
    private var currentNumber = ""
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
        val value = synchronized(stateLock) {
            check(target == null || target === currentCall) {
                "platform call receiver initialized more than once"
            }
            target = currentCall
            currentCallLocked()
        }
        currentCall.value = value
    }

    internal fun providerSnapshotChanged(snapshot: PlatformProviderSnapshot) {
        if (snapshot.domains and CALLS_DOMAIN != 0L) return
        val flow = synchronized(stateLock) {
            if (currentId == null) return
            clearLocked()
            target
        }
        flow?.value = null
    }

    internal fun providerCall(event: PlatformCallEvent) {
        val update = synchronized(stateLock) {
            if (event.state == PlatformProviderController.CALL_ENDED) {
                if (event.id != currentId) return
                clearLocked()
                target to null
            } else {
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
                currentName = event.name
                currentNumber = event.number
                target to currentCallLocked()
            }
        }
        update.first?.value = update.second
        if (event.state != PlatformProviderController.CALL_ENDED &&
            event.name.isEmpty() && event.number.isNotEmpty()) {
            val lookupId = event.id
            val lookupNumber = event.number
            val lookupGeneration = synchronized(stateLock) { currentGeneration }
            commandScope.launch {
                val name = try {
                    lookupContactName(lookupNumber)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w(e) { "platform caller-name lookup failed" }
                    null
                }
                if (name.isNullOrBlank()) return@launch
                val refreshed = synchronized(stateLock) {
                    if (currentId != lookupId || currentGeneration != lookupGeneration ||
                        currentNumber != lookupNumber || currentName.isNotEmpty()) {
                        return@synchronized null
                    }
                    currentName = name
                    target to currentCallLocked()
                }
                refreshed?.first?.value = refreshed.second
            }
        }
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
    }
}
