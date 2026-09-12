/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.packets.blobdb.TimelineIcon
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class PlatformNotificationAppearanceTest {
    @Test
    fun nativeSourceAndIconAliasesDoNotRequireCategoryHints() {
        assertEquals(TimelineIcon.NotificationSignal,
            sailfishNotificationIcon("harbour-whisperfish", null))
        assertEquals(TimelineIcon.NotificationTelegram,
            sailfishNotificationIcon("HARBOUR-FERNSCHREIBER.desktop", null))
        assertEquals(TimelineIcon.NotificationElement,
            sailfishNotificationIcon("unknown", "harbour-sailtrix"))
        assertEquals(TimelineIcon.NotificationReddit,
            sailfishNotificationIcon("harbour-quickddit", null))
        assertEquals(TimelineIcon.NotificationTelegram,
            sailfishNotificationIcon("harbour-yast-client.desktop", null))
        assertEquals(TimelineIcon.GenericEmail,
            sailfishNotificationIcon("com.jolla.email", null))
        assertEquals(TimelineIcon.Settings,
            sailfishNotificationIcon("sailfishos-chum-gui", null))
        assertNull(sailfishNotificationIcon("unrelated-whisperfish-backup", null))
    }

    @Test
    fun sourceAliasTakesPrecedenceOverIconAlias() {
        assertEquals(TimelineIcon.NotificationSignal,
            sailfishNotificationIcon("harbour-whisperfish", "harbour-sailtrix"))
    }
}
