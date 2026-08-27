/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import co.touchlab.kermit.Logger
import io.rebble.libpebblecommon.linux.LinuxDeviceActivity
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.annotations.DBusMemberName
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.interfaces.DBus
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.messages.DBusSignal
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds

@Suppress("FunctionName")
@DBusInterfaceName("com.nokia.mce.request")
internal interface MceRequest : DBusInterface {
    @DBusMemberName("get_inactivity_status")
    fun getInactivityStatus(): Boolean
}

@DBusInterfaceName("com.nokia.mce.signal")
internal interface MceSignal : DBusInterface {
    @DBusMemberName("system_inactivity_ind")
    class SystemInactivityInd(path: String, val inactive: Boolean) :
        DBusSignal(path, inactive)
}

internal fun interface MceActivitySource {
    /** Runs until the connection or MCE owner changes, publishing MCE's inactive flag. */
    suspend fun monitor(publishInactive: (Boolean) -> Unit)
}

/**
 * Caches Sailfish MCE's system-activity state for notification policy.
 *
 * This deliberately follows legacy Rockpool's `system_inactivity_ind`
 * semantics. Display and touch-lock state are different signals and were not
 * part of the old "Disabled When Active" policy. Unknown state is inactive so
 * an MCE failure cannot silently suppress notifications.
 */
internal class SailfishDeviceActivity(
    private val source: MceActivitySource = DbusMceActivitySource(),
    scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    private val retryDelay: Duration = RETRY_DELAY,
) : LinuxDeviceActivity {
    private val logger = Logger.withTag("SailfishDeviceActivity")
    private val active = AtomicBoolean(false)

    init {
        scope.launch {
            while (currentCoroutineContext().isActive) {
                try {
                    source.monitor { inactive -> active.set(!inactive) }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    logger.w { "MCE activity monitor unavailable: ${e.message}" }
                } finally {
                    active.set(false)
                }
                delay(retryDelay)
            }
        }
    }

    override fun isActiveAndUnlocked(): Boolean = active.get()

    private companion object {
        val RETRY_DELAY = 5.seconds
    }
}

/** One private system-bus connection, recreated after MCE or D-Bus restarts. */
private class DbusMceActivitySource : MceActivitySource {
    override suspend fun monitor(publishInactive: (Boolean) -> Unit) {
        val connection = DBusConnectionBuilder.forSystemBus().withShared(false).build()
        try {
            val ownerChanged = CompletableDeferred<Unit>()
            connection.addSigHandler(
                DBus.NameOwnerChanged::class.java,
            ) { signal ->
                if (signal.source == DBUS_SERVICE && signal.name == MCE_SERVICE) {
                    publishInactive(true)
                    ownerChanged.complete(Unit)
                }
            }

            val dbus = connection.getRemoteObject(DBUS_SERVICE, DBUS_PATH, DBus::class.java)
            val owner = dbus.GetNameOwner(MCE_SERVICE)
            connection.addSigHandler(
                MceSignal.SystemInactivityInd::class.java,
                owner,
            ) { signal ->
                if (signal.path == MCE_SIGNAL_PATH && signal.source == owner) {
                    publishInactive(signal.inactive)
                }
            }

            // Register both handlers before the initial read so a transition
            // cannot be missed between taking the snapshot and subscribing.
            val request = connection.getRemoteObject(
                MCE_SERVICE,
                MCE_REQUEST_PATH,
                MceRequest::class.java,
            )
            publishInactive(request.getInactivityStatus())

            while (
                currentCoroutineContext().isActive && connection.isConnected &&
                !ownerChanged.isCompleted
            ) {
                delay(CONNECTION_POLL_INTERVAL)
            }
        } finally {
            runCatching { connection.disconnect() }
        }
    }

    private companion object {
        const val DBUS_SERVICE = "org.freedesktop.DBus"
        const val DBUS_PATH = "/org/freedesktop/DBus"
        const val MCE_SERVICE = "com.nokia.mce"
        const val MCE_REQUEST_PATH = "/com/nokia/mce/request"
        const val MCE_SIGNAL_PATH = "/com/nokia/mce/signal"
        val CONNECTION_POLL_INTERVAL = 1.seconds
    }
}
