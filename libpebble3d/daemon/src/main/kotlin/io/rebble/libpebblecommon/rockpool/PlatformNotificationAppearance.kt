/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.packets.blobdb.TimelineIcon

/** Exact aliases only: source IDs may be RPM names, desktop entries or app-name fallbacks.
 * Keep the original ID untouched so existing mute rules and user overrides remain valid.
 */
internal fun sailfishNotificationIcon(sourceId: String, iconName: String?): TimelineIcon? {
    fun lookup(value: String?): TimelineIcon? = when (value?.lowercase()?.removeSuffix(".desktop")) {
        "harbour-whisperfish", "whisperfish" -> TimelineIcon.NotificationSignal
        "harbour-fernschreiber", "fernschreiber", "depecher", "harbour-depecher",
        "harbour-yast-client", "yast-client", "yast" -> TimelineIcon.NotificationTelegram
        "harbour-sailtrix", "sailtrix" -> TimelineIcon.NotificationElement
        "harbour-piepmatz", "piepmatz" -> TimelineIcon.NotificationTwitter
        "harbour-quickddit", "quickddit" -> TimelineIcon.NotificationReddit
        "jolla-email", "jolla-email-service", "com.jolla.email", "icon-launcher-email" -> TimelineIcon.GenericEmail
        "jolla-messages", "com.jolla.messages", "icon-launcher-messaging" -> TimelineIcon.GenericSms
        "jolla-calendar", "com.jolla.calendar", "icon-launcher-calendar" -> TimelineIcon.TimelineCalendar
        "jolla-clock", "com.jolla.clock", "icon-launcher-clock" -> TimelineIcon.AlarmClock
        "jolla-weather", "com.jolla.weather", "icon-launcher-weather" -> TimelineIcon.TimelineWeather
        "jolla-store", "harbour-storeman", "storeman", "sailfishos-chum-gui" -> TimelineIcon.Settings
        "harbour-pure-maps", "pure-maps" -> TimelineIcon.Location
        else -> null
    }
    return lookup(sourceId) ?: lookup(iconName)
}
