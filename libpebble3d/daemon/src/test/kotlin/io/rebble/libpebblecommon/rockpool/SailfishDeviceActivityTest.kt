/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.annotations.DBusMemberName
import org.freedesktop.dbus.messages.DBusSignal
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.milliseconds

class SailfishDeviceActivityTest {
    @Test
    fun `MCE inactivity is inverted and unknown state is inactive`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val publisher = CompletableDeferred<(Boolean) -> Unit>()
        val activity = SailfishDeviceActivity(
            source = MceActivitySource { publish ->
                publisher.complete(publish)
                awaitCancellation()
            },
            scope = scope,
        )

        try {
            assertFalse(activity.isActiveAndUnlocked())
            val publish = withTimeout(1_000) { publisher.await() }
            publish(false)
            assertTrue(activity.isActiveAndUnlocked())
            publish(true)
            assertFalse(activity.isActiveAndUnlocked())
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `monitor failure fails permissive and retries`() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        var attempts = 0
        val secondAttempt = CompletableDeferred<Unit>()
        val activity = SailfishDeviceActivity(
            source = MceActivitySource { publish ->
                attempts++
                if (attempts == 1) {
                    publish(false)
                    error("MCE disappeared")
                }
                secondAttempt.complete(Unit)
                awaitCancellation()
            },
            scope = scope,
            retryDelay = 10.milliseconds,
        )

        try {
            withTimeout(1_000) { secondAttempt.await() }
            assertEquals(2, attempts)
            assertFalse(activity.isActiveAndUnlocked())
        } finally {
            scope.cancel()
        }
    }

    @Test
    fun `MCE interfaces retain their fixed wire names`() {
        assertEquals(
            "com.nokia.mce.request",
            MceRequest::class.java.getAnnotation(DBusInterfaceName::class.java).value,
        )
        assertEquals(
            "get_inactivity_status",
            MceRequest::class.java.getMethod("getInactivityStatus")
                .getAnnotation(DBusMemberName::class.java).value,
        )
        assertEquals(
            "system_inactivity_ind",
            MceSignal.SystemInactivityInd::class.java
                .getAnnotation(DBusMemberName::class.java).value,
        )
        assertEquals(
            DBusSignal::class.java.name,
            MceSignal.SystemInactivityInd::class.java.superclass?.name,
        )
        val signal = MceSignal.SystemInactivityInd("/com/nokia/mce/signal", false)
        assertEquals("com.nokia.mce.signal", signal.`interface`)
        assertEquals("system_inactivity_ind", signal.name)
        assertEquals("b", signal.sig)
    }
}
