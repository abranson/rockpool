/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private SOCK_SEQPACKET framing shared only by the Sailfish proxy and helper.
 */

#ifndef LIBPEBBLE3D_SAILFISH_WIRE_H
#define LIBPEBBLE3D_SAILFISH_WIRE_H

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace lp3wire {

static const uint16_t kMajor = 1;
static const uint16_t kMinor = 10;
static const size_t kNotificationImageMax = 4 + 128 * 128 * 3;
static const size_t kHeaderSize = 24;
static const size_t kMaxFrameSize = 64 * 1024;

enum FrameType {
    Hello = 1,
    HelloAck = 2,
    Ready = 3,
    Request = 4,
    Complete = 5,
    Cancelled = 6,
    Health = 7,
    Cancel = 8,
    Event = 9,
};

enum Operation {
    TimeGet = 1,
    NotificationCommand = 2,
    CallCommand = 3,
    MediaCommand = 4,
    MessageReply = 5,
    LocationQuery = 6,
    CalendarQuery = 7,
    ContactQuery = 8,
    MessageSend = 9,
    PebbleBondRemove = 10,
};

enum EventType {
    TimeChanged = 1,
    NotificationPosted = 2,
    NotificationClosed = 3,
    CallChanged = 4,
    MediaVolumeChanged = 5,
    CalendarChanged = 6,
    ContactChanged = 7,
};

enum NotificationFlag {
    NotificationHasDefaultAction = 1u << 0,
    NotificationHasReplyAction = 1u << 1,
};

enum NotificationCommand {
    NotificationDismiss = 1,
    NotificationOpen = 2,
};

enum CallCommand {
    CallAnswer = 1,
    CallHangUp = 2,
    CallSilence = 3,
};

enum CallState {
    CallEnded = 0,
    CallRinging = 1,
    CallDialing = 2,
    CallActive = 3,
    CallHeld = 4,
};

enum MediaFlag {
    MediaSystemVolume = 1u << 0,
};

enum MediaCommand {
    MediaVolumeUp = 4,
    MediaVolumeDown = 5,
};

enum Domain {
    DomainNotifications = 1u << 0,
    DomainMessaging = 1u << 1,
    DomainMedia = 1u << 2,
    DomainCalls = 1u << 3,
    DomainCalendar = 1u << 4,
    DomainContacts = 1u << 5,
    DomainLocation = 1u << 6,
    DomainTime = 1u << 7,
};

enum IoResult {
    IoFrame,
    IoWouldBlock,
    IoClosed,
    IoInvalid,
    IoError,
};

struct Frame {
    uint16_t type;
    uint64_t requestId;
    std::vector<uint8_t> payload;
};

struct TimeState {
    int32_t utcOffsetSeconds;
    int64_t unixMs;
    uint32_t is24Hour;
};

struct NotificationData {
    uint32_t flags;
    int64_t timestampMs;
    uint32_t closeReason;
    std::string id;
    std::string replacesId;
    std::string applicationId;
    std::string applicationName;
    std::string title;
    std::string body;
    std::string category;
    std::string iconName;
    std::vector<uint8_t> image;
};

struct NotificationCommandData {
    uint32_t command;
    std::string id;
};

struct MessageReplyData {
    std::string notificationId;
    std::string text;
};

struct MessageSendData {
    std::string accountId;
    std::string recipient;
    std::string text;
};

struct PebbleBondRemoveData {
    uint32_t adapterIndex;
    uint8_t address[6];
};

struct CallData {
    uint32_t state;
    std::string id;
    std::string name;
    std::string number;
};

struct CallCommandData {
    uint32_t command;
    std::string id;
};

struct MediaVolumeData {
    uint32_t flags;
    int32_t volumePercent;
};

struct MediaCommandData {
    uint32_t command;
};

struct LocationQueryData {
    uint32_t accuracy;
    uint32_t timeoutMs;
};

struct LocationData {
    int32_t latitudeE7;
    int32_t longitudeE7;
    int32_t accuracyM;
    int64_t timestampMs;
};

enum CalendarQueryKind {
    CalendarQueryCalendars = 1,
    CalendarQueryEvents = 2,
};

enum CalendarFlag {
    CalendarVisible = 1u << 0,
    CalendarEnabled = 1u << 1,
    CalendarSyncEvents = 1u << 2,
};

enum CalendarEventFlag {
    CalendarEventAllDay = 1u << 0,
    CalendarEventRecurs = 1u << 1,
};

enum CalendarAttendeeFlag {
    CalendarAttendeeOrganizer = 1u << 0,
    CalendarAttendeeCurrentUser = 1u << 1,
};

struct CalendarQueryData {
    uint32_t kind;
    uint32_t maxRecords;
    uint32_t offset;
    int64_t startMs;
    int64_t endMs;
    std::string calendarId;
};

struct CalendarData {
    uint32_t flags;
    uint32_t colorArgb;
    std::string id;
    std::string name;
    std::string ownerName;
    std::string ownerId;
};

struct CalendarAttendeeData {
    uint32_t flags;
    uint32_t role;
    uint32_t status;
    std::string name;
    std::string email;
};

struct CalendarEventData {
    uint32_t flags;
    uint32_t availability;
    uint32_t status;
    int64_t startMs;
    int64_t endMs;
    std::string id;
    std::string calendarId;
    std::string baseEventId;
    std::string title;
    std::string description;
    std::string location;
    std::vector<CalendarAttendeeData> attendees;
    std::vector<int32_t> reminderMinutes;
};

struct CalendarReplyData {
    uint32_t kind;
    uint32_t nextOffset;
    std::vector<CalendarData> calendars;
    std::vector<CalendarEventData> events;
};

enum ContactQueryKind {
    ContactQueryList = 1,
    ContactQueryPhone = 2,
};

struct ContactQueryData {
    uint32_t kind;
    uint32_t maxRecords;
    uint32_t offset;
    std::string query;
};

struct ContactData {
    uint32_t flags;
    std::string id;
    std::string displayName;
    std::string phoneNumber;
    std::vector<uint8_t> avatar;
};

struct ContactReplyData {
    uint32_t kind;
    uint32_t nextOffset;
    std::vector<ContactData> contacts;
};

struct HealthState {
    uint64_t readyDomains;
    uint64_t degradedDomains;
    uint64_t failedDomains;
};

static const size_t kNotificationIdMax = 64;
static const size_t kNotificationApplicationIdMax = 256;
static const size_t kNotificationApplicationNameMax = 256;
static const size_t kNotificationTitleMax = 512;
static const size_t kNotificationBodyMax = 4096;
static const size_t kNotificationCategoryMax = 128;
static const size_t kNotificationIconNameMax = 128;
static const size_t kMessageConversationIdMax = 64;
static const size_t kMessageTextMax = 512;
static const size_t kMessageAccountIdMax = 512;
static const size_t kMessageRecipientMax = 512;
static const size_t kCallIdMax = 128;
static const size_t kCallNameMax = 256;
static const size_t kCallNumberMax = 256;
static const uint32_t kLocationCoarse = 1;
static const uint32_t kLocationFine = 2;
static const uint32_t kLocationTimeoutMaxMs = 30000;
static const uint32_t kCalendarPageMax = 64;
static const uint32_t kCalendarTotalMax = 512;
static const uint32_t kCalendarAttendeeMax = 16;
static const uint32_t kCalendarReminderMax = 8;
static const int64_t kCalendarRangeMaxMs = INT64_C(370) * 24 * 60 * 60 * 1000;
static const size_t kCalendarIdMax = 256;
static const size_t kCalendarNameMax = 256;
static const size_t kCalendarOwnerMax = 256;
static const size_t kCalendarEventIdMax = 256;
static const size_t kCalendarTitleMax = 512;
static const size_t kCalendarDescriptionMax = 1024;
static const size_t kCalendarLocationMax = 512;
static const size_t kCalendarAttendeeTextMax = 256;
static const uint32_t kContactPageMax = 64;
static const uint32_t kContactTotalMax = 4096;
static const size_t kContactIdMax = 256;
static const size_t kContactNameMax = 256;
static const size_t kContactNumberMax = 256;
static const size_t kContactAvatarMax = 16 * 1024;

inline void put16(uint8_t *data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xff);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

inline void put32(uint8_t *data, uint32_t value) {
    for (unsigned int index = 0; index < 4; ++index) {
        data[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
    }
}

inline void put64(uint8_t *data, uint64_t value) {
    for (unsigned int index = 0; index < 8; ++index) {
        data[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
    }
}

inline uint16_t get16(const uint8_t *data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

inline uint32_t get32(const uint8_t *data) {
    uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index) {
        value |= static_cast<uint32_t>(data[index]) << (index * 8);
    }
    return value;
}

inline uint64_t get64(const uint8_t *data) {
    uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(data[index]) << (index * 8);
    }
    return value;
}

inline bool validStatus(uint32_t status);

inline bool validHealthState(const HealthState &health) {
    const uint64_t supported = DomainNotifications | DomainMessaging |
                               DomainMedia | DomainCalls | DomainCalendar |
                               DomainContacts | DomainLocation | DomainTime;
    return ((health.readyDomains | health.degradedDomains |
             health.failedDomains) & ~supported) == 0 &&
           (health.readyDomains & health.degradedDomains) == 0 &&
           (health.readyDomains & health.failedDomains) == 0 &&
           (health.degradedDomains & health.failedDomains) == 0 &&
           (health.readyDomains | health.degradedDomains |
            health.failedDomains) == supported;
}

inline bool encodeHealth(const HealthState &health,
                         std::vector<uint8_t> *payload) {
    if (payload == NULL || !validHealthState(health)) {
        return false;
    }
    payload->assign(24, 0);
    put64(&(*payload)[0], health.readyDomains);
    put64(&(*payload)[8], health.degradedDomains);
    put64(&(*payload)[16], health.failedDomains);
    return true;
}

inline bool decodeHealth(const std::vector<uint8_t> &payload,
                         HealthState *health) {
    HealthState decoded;
    if (health == NULL || payload.size() != 24) {
        return false;
    }
    decoded.readyDomains = get64(&payload[0]);
    decoded.degradedDomains = get64(&payload[8]);
    decoded.failedDomains = get64(&payload[16]);
    if (!validHealthState(decoded)) {
        return false;
    }
    *health = decoded;
    return true;
}

inline bool validUtf8(const std::string &text) {
    size_t index = 0;
    while (index < text.size()) {
        const uint8_t first = static_cast<uint8_t>(text[index]);
        if (first == 0) {
            return false;
        }
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) {
            if (index + 1 >= text.size() ||
                (static_cast<uint8_t>(text[index + 1]) & 0xc0) != 0x80) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            if (index + 2 >= text.size()) {
                return false;
            }
            const uint8_t second = static_cast<uint8_t>(text[index + 1]);
            const uint8_t third = static_cast<uint8_t>(text[index + 2]);
            if ((third & 0xc0) != 0x80 ||
                (first == 0xe0 ? second < 0xa0 || second > 0xbf :
                 first == 0xed ? second < 0x80 || second > 0x9f :
                 (second & 0xc0) != 0x80)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            if (index + 3 >= text.size()) {
                return false;
            }
            const uint8_t second = static_cast<uint8_t>(text[index + 1]);
            const uint8_t third = static_cast<uint8_t>(text[index + 2]);
            const uint8_t fourth = static_cast<uint8_t>(text[index + 3]);
            if ((third & 0xc0) != 0x80 || (fourth & 0xc0) != 0x80 ||
                (first == 0xf0 ? second < 0x90 || second > 0xbf :
                 first == 0xf4 ? second < 0x80 || second > 0x8f :
                 (second & 0xc0) != 0x80)) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

inline bool validText(const std::string &text, size_t maximum,
                      bool allowEmpty = true) {
    return (allowEmpty || !text.empty()) && text.size() <= maximum &&
           validUtf8(text);
}

inline bool validNotificationId(const std::string &id,
                                bool allowEmpty = false) {
    uint64_t value = 0;
    if (id.empty()) {
        return allowEmpty;
    }
    if (id.size() > kNotificationIdMax) {
        return false;
    }
    for (size_t index = 0; index < id.size(); ++index) {
        const uint8_t character = static_cast<uint8_t>(id[index]);
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + character - '0';
        if (value > UINT32_MAX) {
            return false;
        }
    }
    return value != 0;
}

inline bool validCallId(const std::string &id, bool allowEmpty = false) {
    if (id.empty()) {
        return allowEmpty;
    }
    if (id.size() > kCallIdMax) {
        return false;
    }
    for (size_t index = 0; index < id.size(); ++index) {
        const uint8_t character = static_cast<uint8_t>(id[index]);
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return true;
}

inline bool validCall(const CallData &call) {
    if (call.state > CallHeld || !validCallId(call.id) ||
        !validText(call.name, kCallNameMax) ||
        !validText(call.number, kCallNumberMax)) {
        return false;
    }
    return call.state != CallEnded ||
           (call.name.empty() && call.number.empty());
}

inline bool validCallCommand(const CallCommandData &command) {
    if (command.command == CallSilence) {
        return command.id.empty();
    }
    return (command.command == CallAnswer || command.command == CallHangUp) &&
           validCallId(command.id);
}

inline bool validMediaVolume(const MediaVolumeData &volume) {
    return volume.flags == MediaSystemVolume && volume.volumePercent >= 0 &&
           volume.volumePercent <= 100;
}

inline bool validMediaCommand(const MediaCommandData &command) {
    return command.command == MediaVolumeUp || command.command == MediaVolumeDown;
}

inline bool validNotificationImage(const std::vector<uint8_t> &image) {
    if (image.empty()) return true;
    if (image.size() < 7 || image.size() > kNotificationImageMax) return false;
    const uint32_t width = get16(&image[0]);
    const uint32_t height = get16(&image[2]);
    return width > 0 && width <= 128 && height > 0 && height <= 128 &&
           image.size() == 4 + width * height * 3;
}

inline bool validNotification(uint16_t eventType,
                              const NotificationData &notification) {
    if (!validNotificationId(notification.id) || !validNotificationImage(notification.image)) {
        return false;
    }
    if (eventType == NotificationClosed) {
        return notification.flags == 0 && notification.timestampMs == 0 &&
               notification.closeReason <= 4 && notification.replacesId.empty() &&
               notification.applicationId.empty() &&
               notification.applicationName.empty() && notification.title.empty() &&
               notification.body.empty() && notification.category.empty() &&
               notification.iconName.empty() && notification.image.empty();
    }
    return eventType == NotificationPosted && notification.closeReason == 0 &&
           (notification.flags & ~(NotificationHasDefaultAction |
                                   NotificationHasReplyAction)) == 0 &&
           validNotificationId(notification.replacesId, true) &&
           validText(notification.applicationId,
                     kNotificationApplicationIdMax, false) &&
           validText(notification.applicationName,
                     kNotificationApplicationNameMax) &&
           validText(notification.title, kNotificationTitleMax) &&
           validText(notification.body, kNotificationBodyMax) &&
           validText(notification.category, kNotificationCategoryMax) &&
           validText(notification.iconName, kNotificationIconNameMax) &&
           (!notification.title.empty() || !notification.body.empty());
}

inline void appendText(std::vector<uint8_t> *payload,
                       const std::string &text) {
    payload->insert(payload->end(), text.begin(), text.end());
}

inline bool readText(const std::vector<uint8_t> &payload, size_t *offset,
                     uint32_t length, std::string *text) {
    if (offset == NULL || text == NULL || *offset > payload.size() ||
        length > payload.size() - *offset) {
        return false;
    }
    if (length == 0) {
        text->clear();
    } else {
        text->assign(reinterpret_cast<const char *>(&payload[*offset]), length);
    }
    *offset += length;
    return true;
}

inline bool encodeNotificationEvent(uint16_t eventType,
                                    const NotificationData &notification,
                                    std::vector<uint8_t> *payload) {
    if (payload == NULL || !validNotification(eventType, notification)) {
        return false;
    }
    payload->assign(52, 0);
    put16(&(*payload)[0], eventType);
    put32(&(*payload)[4], notification.flags);
    put64(&(*payload)[8], static_cast<uint64_t>(notification.timestampMs));
    put32(&(*payload)[16], notification.closeReason);
    put32(&(*payload)[20], static_cast<uint32_t>(notification.id.size()));
    put32(&(*payload)[24], static_cast<uint32_t>(notification.replacesId.size()));
    put32(&(*payload)[28], static_cast<uint32_t>(notification.applicationId.size()));
    put32(&(*payload)[32], static_cast<uint32_t>(notification.applicationName.size()));
    put32(&(*payload)[36], static_cast<uint32_t>(notification.title.size()));
    put32(&(*payload)[40], static_cast<uint32_t>(notification.body.size()));
    put32(&(*payload)[44], static_cast<uint32_t>(notification.category.size()));
    put32(&(*payload)[48], static_cast<uint32_t>(notification.iconName.size()));
    appendText(payload, notification.id);
    appendText(payload, notification.replacesId);
    appendText(payload, notification.applicationId);
    appendText(payload, notification.applicationName);
    appendText(payload, notification.title);
    appendText(payload, notification.body);
    appendText(payload, notification.category);
    appendText(payload, notification.iconName);
    payload->insert(payload->end(), notification.image.begin(), notification.image.end());
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeNotificationEvent(const std::vector<uint8_t> &payload,
                                    uint16_t *eventType,
                                    NotificationData *notification) {
    NotificationData decoded;
    uint16_t decodedEvent;
    size_t offset = 52;

    if (eventType == NULL || notification == NULL || payload.size() < offset ||
        get16(&payload[2]) != 0) {
        return false;
    }
    decodedEvent = get16(&payload[0]);
    decoded.flags = get32(&payload[4]);
    decoded.timestampMs = static_cast<int64_t>(get64(&payload[8]));
    decoded.closeReason = get32(&payload[16]);
    if (!readText(payload, &offset, get32(&payload[20]), &decoded.id) ||
        !readText(payload, &offset, get32(&payload[24]), &decoded.replacesId) ||
        !readText(payload, &offset, get32(&payload[28]), &decoded.applicationId) ||
        !readText(payload, &offset, get32(&payload[32]), &decoded.applicationName) ||
        !readText(payload, &offset, get32(&payload[36]), &decoded.title) ||
        !readText(payload, &offset, get32(&payload[40]), &decoded.body) ||
        !readText(payload, &offset, get32(&payload[44]), &decoded.category) ||
        !readText(payload, &offset, get32(&payload[48]), &decoded.iconName) ||
        offset > payload.size()) {
        return false;
    }
    decoded.image.assign(payload.begin() + offset, payload.end());
    if (!validNotification(decodedEvent, decoded)) return false;
    *eventType = decodedEvent;
    *notification = decoded;
    return true;
}

inline bool validNotificationCommand(const NotificationCommandData &command) {
    return (command.command == NotificationDismiss ||
            command.command == NotificationOpen) &&
           validNotificationId(command.id);
}

inline bool encodeNotificationCommand(const NotificationCommandData &command,
                                      std::vector<uint8_t> *payload) {
    if (payload == NULL || !validNotificationCommand(command)) {
        return false;
    }
    payload->assign(12, 0);
    put16(&(*payload)[0], NotificationCommand);
    put32(&(*payload)[4], command.command);
    put32(&(*payload)[8], static_cast<uint32_t>(command.id.size()));
    appendText(payload, command.id);
    return true;
}

inline bool decodeNotificationCommand(const std::vector<uint8_t> &payload,
                                      NotificationCommandData *command) {
    NotificationCommandData decoded;
    size_t offset = 12;
    if (command == NULL || payload.size() < offset ||
        get16(&payload[0]) != NotificationCommand || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.command = get32(&payload[4]);
    if (!readText(payload, &offset, get32(&payload[8]), &decoded.id) ||
        offset != payload.size() || !validNotificationCommand(decoded)) {
        return false;
    }
    *command = decoded;
    return true;
}

inline bool validMessageReply(const MessageReplyData &reply) {
    return validNotificationId(reply.notificationId) &&
           reply.notificationId[0] != '0' &&
           validText(reply.text, kMessageTextMax, false);
}

inline bool encodeMessageReply(const MessageReplyData &reply,
                               std::vector<uint8_t> *payload) {
    if (payload == NULL || !validMessageReply(reply)) {
        return false;
    }
    payload->assign(12, 0);
    put16(&(*payload)[0], MessageReply);
    put32(&(*payload)[4], static_cast<uint32_t>(reply.notificationId.size()));
    put32(&(*payload)[8], static_cast<uint32_t>(reply.text.size()));
    appendText(payload, reply.notificationId);
    appendText(payload, reply.text);
    return true;
}

inline bool decodeMessageReply(const std::vector<uint8_t> &payload,
                               MessageReplyData *reply) {
    MessageReplyData decoded;
    size_t offset = 12;
    if (reply == NULL || payload.size() < offset ||
        get16(&payload[0]) != MessageReply || get16(&payload[2]) != 0 ||
        !readText(payload, &offset, get32(&payload[4]),
                  &decoded.notificationId) ||
        !readText(payload, &offset, get32(&payload[8]), &decoded.text) ||
        offset != payload.size() || !validMessageReply(decoded)) {
        return false;
    }
    *reply = decoded;
    return true;
}

inline bool validMessageSend(const MessageSendData &message) {
    static const char accountPrefix[] =
        "/org/freedesktop/Telepathy/Account/";
    return validText(message.accountId, kMessageAccountIdMax, false) &&
           message.accountId.size() > sizeof(accountPrefix) - 1 &&
           message.accountId.compare(0, sizeof(accountPrefix) - 1,
                                     accountPrefix) == 0 &&
           validText(message.recipient, kMessageRecipientMax, false) &&
           validText(message.text, kMessageTextMax, false);
}

inline bool encodeMessageSend(const MessageSendData &message,
                              std::vector<uint8_t> *payload) {
    if (payload == NULL || !validMessageSend(message)) return false;
    payload->assign(16, 0);
    put16(&(*payload)[0], MessageSend);
    put32(&(*payload)[4], static_cast<uint32_t>(message.accountId.size()));
    put32(&(*payload)[8], static_cast<uint32_t>(message.recipient.size()));
    put32(&(*payload)[12], static_cast<uint32_t>(message.text.size()));
    appendText(payload, message.accountId);
    appendText(payload, message.recipient);
    appendText(payload, message.text);
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeMessageSend(const std::vector<uint8_t> &payload,
                              MessageSendData *message) {
    MessageSendData decoded;
    size_t offset = 16;
    if (message == NULL || payload.size() < offset ||
        get16(&payload[0]) != MessageSend || get16(&payload[2]) != 0 ||
        !readText(payload, &offset, get32(&payload[4]), &decoded.accountId) ||
        !readText(payload, &offset, get32(&payload[8]), &decoded.recipient) ||
        !readText(payload, &offset, get32(&payload[12]), &decoded.text) ||
        offset != payload.size() || !validMessageSend(decoded)) {
        return false;
    }
    *message = decoded;
    return true;
}

inline bool validPebbleBondRemove(const PebbleBondRemoveData &request) {
    bool anyNonzero = false;
    bool anyNotBroadcast = false;
    for (size_t index = 0; index < sizeof(request.address); ++index) {
        anyNonzero = anyNonzero || request.address[index] != 0;
        anyNotBroadcast = anyNotBroadcast || request.address[index] != 0xff;
    }
    return anyNonzero && anyNotBroadcast;
}

inline bool encodePebbleBondRemove(const PebbleBondRemoveData &request,
                                   std::vector<uint8_t> *payload) {
    if (payload == NULL || !validPebbleBondRemove(request)) {
        return false;
    }
    payload->assign(16, 0);
    put16(&(*payload)[0], PebbleBondRemove);
    put32(&(*payload)[4], request.adapterIndex);
    memcpy(&(*payload)[8], request.address, sizeof(request.address));
    return true;
}

inline bool decodePebbleBondRemove(const std::vector<uint8_t> &payload,
                                   PebbleBondRemoveData *request) {
    PebbleBondRemoveData decoded = {};
    if (request == NULL || payload.size() != 16 ||
        get16(&payload[0]) != PebbleBondRemove || get16(&payload[2]) != 0 ||
        payload[14] != 0 || payload[15] != 0) {
        return false;
    }
    decoded.adapterIndex = get32(&payload[4]);
    memcpy(decoded.address, &payload[8], sizeof(decoded.address));
    if (!validPebbleBondRemove(decoded)) {
        return false;
    }
    *request = decoded;
    return true;
}

inline bool encodeCallChanged(const CallData &call,
                              std::vector<uint8_t> *payload) {
    if (payload == NULL || !validCall(call)) {
        return false;
    }
    payload->assign(20, 0);
    put16(&(*payload)[0], CallChanged);
    put32(&(*payload)[4], call.state);
    put32(&(*payload)[8], static_cast<uint32_t>(call.id.size()));
    put32(&(*payload)[12], static_cast<uint32_t>(call.name.size()));
    put32(&(*payload)[16], static_cast<uint32_t>(call.number.size()));
    appendText(payload, call.id);
    appendText(payload, call.name);
    appendText(payload, call.number);
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeCallChanged(const std::vector<uint8_t> &payload,
                              CallData *call) {
    CallData decoded;
    size_t offset = 20;
    if (call == NULL || payload.size() < offset ||
        get16(&payload[0]) != CallChanged || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.state = get32(&payload[4]);
    if (!readText(payload, &offset, get32(&payload[8]), &decoded.id) ||
        !readText(payload, &offset, get32(&payload[12]), &decoded.name) ||
        !readText(payload, &offset, get32(&payload[16]), &decoded.number) ||
        offset != payload.size() || !validCall(decoded)) {
        return false;
    }
    *call = decoded;
    return true;
}

inline bool encodeCallCommand(const CallCommandData &command,
                              std::vector<uint8_t> *payload) {
    if (payload == NULL || !validCallCommand(command)) {
        return false;
    }
    payload->assign(12, 0);
    put16(&(*payload)[0], CallCommand);
    put32(&(*payload)[4], command.command);
    put32(&(*payload)[8], static_cast<uint32_t>(command.id.size()));
    appendText(payload, command.id);
    return true;
}

inline bool decodeCallCommand(const std::vector<uint8_t> &payload,
                              CallCommandData *command) {
    CallCommandData decoded;
    size_t offset = 12;
    if (command == NULL || payload.size() < offset ||
        get16(&payload[0]) != CallCommand || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.command = get32(&payload[4]);
    if (!readText(payload, &offset, get32(&payload[8]), &decoded.id) ||
        offset != payload.size() || !validCallCommand(decoded)) {
        return false;
    }
    *command = decoded;
    return true;
}

inline bool encodeMediaVolumeChanged(const MediaVolumeData &volume,
                                     std::vector<uint8_t> *payload) {
    if (payload == NULL || !validMediaVolume(volume)) {
        return false;
    }
    payload->assign(12, 0);
    put16(&(*payload)[0], MediaVolumeChanged);
    put32(&(*payload)[4], volume.flags);
    put32(&(*payload)[8], static_cast<uint32_t>(volume.volumePercent));
    return true;
}

inline bool decodeMediaVolumeChanged(const std::vector<uint8_t> &payload,
                                     MediaVolumeData *volume) {
    MediaVolumeData decoded;
    if (volume == NULL || payload.size() != 12 ||
        get16(&payload[0]) != MediaVolumeChanged || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.flags = get32(&payload[4]);
    decoded.volumePercent = static_cast<int32_t>(get32(&payload[8]));
    if (!validMediaVolume(decoded)) {
        return false;
    }
    *volume = decoded;
    return true;
}

inline bool encodeMediaCommand(const MediaCommandData &command,
                               std::vector<uint8_t> *payload) {
    if (payload == NULL || !validMediaCommand(command)) {
        return false;
    }
    payload->assign(8, 0);
    put16(&(*payload)[0], MediaCommand);
    put32(&(*payload)[4], command.command);
    return true;
}

inline bool decodeMediaCommand(const std::vector<uint8_t> &payload,
                               MediaCommandData *command) {
    MediaCommandData decoded;
    if (command == NULL || payload.size() != 8 ||
        get16(&payload[0]) != MediaCommand || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.command = get32(&payload[4]);
    if (!validMediaCommand(decoded)) {
        return false;
    }
    *command = decoded;
    return true;
}

inline bool encodeStatusReply(uint16_t operation, uint32_t status,
                              std::vector<uint8_t> *payload) {
    if (payload == NULL || (operation != NotificationCommand &&
                            operation != MessageReply &&
                            operation != MessageSend &&
                            operation != CallCommand &&
                            operation != MediaCommand &&
                            operation != PebbleBondRemove) ||
        !validStatus(status)) {
        return false;
    }
    payload->assign(8, 0);
    put16(&(*payload)[0], operation);
    put32(&(*payload)[4], status);
    return true;
}

inline bool decodeStatusReply(const std::vector<uint8_t> &payload,
                              uint16_t expectedOperation, uint32_t *status) {
    if (status == NULL || payload.size() != 8 ||
        get16(&payload[0]) != expectedOperation || get16(&payload[2]) != 0 ||
        !validStatus(get32(&payload[4]))) {
        return false;
    }
    *status = get32(&payload[4]);
    return true;
}

inline bool validStatus(uint32_t status) {
    return status <= 8;
}

inline bool validTimeState(const TimeState &state) {
    return state.utcOffsetSeconds >= -24 * 60 * 60 &&
           state.utcOffsetSeconds <= 24 * 60 * 60 &&
           state.is24Hour <= 1;
}

inline bool encodeTimeGet(std::vector<uint8_t> *payload) {
    if (payload == NULL) {
        return false;
    }
    payload->assign(4, 0);
    put16(&(*payload)[0], TimeGet);
    return true;
}

inline bool decodeTimeGet(const std::vector<uint8_t> &payload) {
    return payload.size() == 4 && get16(&payload[0]) == TimeGet &&
           get16(&payload[2]) == 0;
}

inline bool encodeTimeReply(uint32_t status, const TimeState &state,
                            std::vector<uint8_t> *payload) {
    if (payload == NULL || !validStatus(status) ||
        (status == 0 ? !validTimeState(state) :
         state.utcOffsetSeconds != 0 || state.unixMs != 0 || state.is24Hour != 0)) {
        return false;
    }
    payload->assign(24, 0);
    put16(&(*payload)[0], TimeGet);
    put32(&(*payload)[4], status);
    put32(&(*payload)[8], static_cast<uint32_t>(state.utcOffsetSeconds));
    put64(&(*payload)[12], static_cast<uint64_t>(state.unixMs));
    put32(&(*payload)[20], state.is24Hour);
    return true;
}

inline bool decodeTimeReply(const std::vector<uint8_t> &payload,
                            uint32_t *status, TimeState *state) {
    TimeState decoded;
    uint32_t decodedStatus;

    if (status == NULL || state == NULL || payload.size() != 24 ||
        get16(&payload[0]) != TimeGet || get16(&payload[2]) != 0) {
        return false;
    }
    decodedStatus = get32(&payload[4]);
    decoded.utcOffsetSeconds = static_cast<int32_t>(get32(&payload[8]));
    decoded.unixMs = static_cast<int64_t>(get64(&payload[12]));
    decoded.is24Hour = get32(&payload[20]);
    if (!validStatus(decodedStatus) ||
        (decodedStatus == 0 ? !validTimeState(decoded) :
         decoded.utcOffsetSeconds != 0 || decoded.unixMs != 0 ||
             decoded.is24Hour != 0)) {
        return false;
    }
    *status = decodedStatus;
    *state = decoded;
    return true;
}

inline bool encodeTimeChanged(const TimeState &state,
                              std::vector<uint8_t> *payload) {
    if (payload == NULL || !validTimeState(state)) {
        return false;
    }
    payload->assign(20, 0);
    put16(&(*payload)[0], TimeChanged);
    put32(&(*payload)[4], static_cast<uint32_t>(state.utcOffsetSeconds));
    put64(&(*payload)[8], static_cast<uint64_t>(state.unixMs));
    put32(&(*payload)[16], state.is24Hour);
    return true;
}

inline bool decodeTimeChanged(const std::vector<uint8_t> &payload,
                              TimeState *state) {
    TimeState decoded;

    if (state == NULL || payload.size() != 20 ||
        get16(&payload[0]) != TimeChanged || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.utcOffsetSeconds = static_cast<int32_t>(get32(&payload[4]));
    decoded.unixMs = static_cast<int64_t>(get64(&payload[8]));
    decoded.is24Hour = get32(&payload[16]);
    if (!validTimeState(decoded)) {
        return false;
    }
    *state = decoded;
    return true;
}

inline bool validLocationAccuracy(uint32_t accuracy) {
    return accuracy == kLocationCoarse || accuracy == kLocationFine;
}

inline bool validLocationQuery(const LocationQueryData &query) {
    return validLocationAccuracy(query.accuracy) && query.timeoutMs != 0 &&
           query.timeoutMs <= kLocationTimeoutMaxMs;
}

inline bool validLocation(const LocationData &location) {
    return location.latitudeE7 >= -900000000 && location.latitudeE7 <= 900000000 &&
           location.longitudeE7 >= -1800000000 &&
           location.longitudeE7 <= 1800000000 && location.accuracyM >= 0 &&
           location.timestampMs > 0;
}

inline bool emptyLocation(const LocationData &location) {
    return location.latitudeE7 == 0 && location.longitudeE7 == 0 &&
           location.accuracyM == 0 && location.timestampMs == 0;
}

inline bool encodeLocationQuery(const LocationQueryData &query,
                                std::vector<uint8_t> *payload) {
    if (payload == NULL || !validLocationQuery(query)) {
        return false;
    }
    payload->assign(12, 0);
    put16(&(*payload)[0], LocationQuery);
    put32(&(*payload)[4], query.accuracy);
    put32(&(*payload)[8], query.timeoutMs);
    return true;
}

inline bool decodeLocationQuery(const std::vector<uint8_t> &payload,
                                LocationQueryData *query) {
    LocationQueryData decoded;

    if (query == NULL || payload.size() != 12 ||
        get16(&payload[0]) != LocationQuery || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.accuracy = get32(&payload[4]);
    decoded.timeoutMs = get32(&payload[8]);
    if (!validLocationQuery(decoded)) {
        return false;
    }
    *query = decoded;
    return true;
}

inline bool encodeLocationReply(uint32_t status, const LocationData &location,
                                std::vector<uint8_t> *payload) {
    if (payload == NULL || !validStatus(status) ||
        (status == 0 ? !validLocation(location) : !emptyLocation(location))) {
        return false;
    }
    payload->assign(28, 0);
    put16(&(*payload)[0], LocationQuery);
    put32(&(*payload)[4], status);
    put32(&(*payload)[8], static_cast<uint32_t>(location.latitudeE7));
    put32(&(*payload)[12], static_cast<uint32_t>(location.longitudeE7));
    put32(&(*payload)[16], static_cast<uint32_t>(location.accuracyM));
    put64(&(*payload)[20], static_cast<uint64_t>(location.timestampMs));
    return true;
}

inline bool decodeLocationReply(const std::vector<uint8_t> &payload,
                                uint32_t *status, LocationData *location) {
    LocationData decoded;
    uint32_t decodedStatus;

    if (status == NULL || location == NULL || payload.size() != 28 ||
        get16(&payload[0]) != LocationQuery || get16(&payload[2]) != 0) {
        return false;
    }
    decodedStatus = get32(&payload[4]);
    decoded.latitudeE7 = static_cast<int32_t>(get32(&payload[8]));
    decoded.longitudeE7 = static_cast<int32_t>(get32(&payload[12]));
    decoded.accuracyM = static_cast<int32_t>(get32(&payload[16]));
    decoded.timestampMs = static_cast<int64_t>(get64(&payload[20]));
    if (!validStatus(decodedStatus) ||
        (decodedStatus == 0 ? !validLocation(decoded) : !emptyLocation(decoded))) {
        return false;
    }
    *status = decodedStatus;
    *location = decoded;
    return true;
}

inline bool validCalendarKind(uint32_t kind) {
    return kind == CalendarQueryCalendars || kind == CalendarQueryEvents;
}

inline bool validCalendarQuery(const CalendarQueryData &query) {
    if (!validCalendarKind(query.kind) || query.maxRecords == 0 ||
        query.maxRecords > kCalendarPageMax || query.offset > kCalendarTotalMax) {
        return false;
    }
    if (query.kind == CalendarQueryCalendars) {
        return query.startMs == 0 && query.endMs == 0 &&
               query.calendarId.empty();
    }
    if (!validText(query.calendarId, kCalendarIdMax, false) ||
        query.startMs >= query.endMs) {
        return false;
    }
    return static_cast<uint64_t>(query.endMs) -
               static_cast<uint64_t>(query.startMs) <=
           static_cast<uint64_t>(kCalendarRangeMaxMs);
}

inline bool validCalendar(const CalendarData &calendar) {
    return (calendar.flags & ~(CalendarVisible | CalendarEnabled |
                               CalendarSyncEvents)) == 0 &&
           validText(calendar.id, kCalendarIdMax, false) &&
           validText(calendar.name, kCalendarNameMax, false) &&
           validText(calendar.ownerName, kCalendarOwnerMax) &&
           validText(calendar.ownerId, kCalendarOwnerMax);
}

inline bool validCalendarAttendee(const CalendarAttendeeData &attendee) {
    return (attendee.flags & ~(CalendarAttendeeOrganizer |
                               CalendarAttendeeCurrentUser)) == 0 &&
           attendee.role <= 3 && attendee.status <= 4 &&
           validText(attendee.name, kCalendarAttendeeTextMax) &&
           validText(attendee.email, kCalendarAttendeeTextMax) &&
           (!attendee.name.empty() || !attendee.email.empty());
}

inline bool validCalendarEvent(const CalendarEventData &event) {
    if ((event.flags & ~(CalendarEventAllDay | CalendarEventRecurs)) != 0 ||
        event.availability > 3 || event.status > 3 ||
        event.startMs >= event.endMs ||
        !validText(event.id, kCalendarEventIdMax, false) ||
        !validText(event.calendarId, kCalendarIdMax, false) ||
        !validText(event.baseEventId, kCalendarEventIdMax, false) ||
        !validText(event.title, kCalendarTitleMax, false) ||
        !validText(event.description, kCalendarDescriptionMax) ||
        !validText(event.location, kCalendarLocationMax) ||
        event.attendees.size() > kCalendarAttendeeMax ||
        event.reminderMinutes.size() > kCalendarReminderMax) {
        return false;
    }
    for (size_t index = 0; index < event.attendees.size(); ++index) {
        if (!validCalendarAttendee(event.attendees[index])) {
            return false;
        }
    }
    for (size_t index = 0; index < event.reminderMinutes.size(); ++index) {
        if (event.reminderMinutes[index] < 0 ||
            event.reminderMinutes[index] > 366 * 24 * 60) {
            return false;
        }
    }
    return true;
}

inline bool encodeCalendarQuery(const CalendarQueryData &query,
                                std::vector<uint8_t> *payload) {
    if (payload == NULL || !validCalendarQuery(query)) {
        return false;
    }
    payload->assign(36, 0);
    put16(&(*payload)[0], CalendarQuery);
    put32(&(*payload)[4], query.kind);
    put32(&(*payload)[8], query.maxRecords);
    put32(&(*payload)[12], query.offset);
    put64(&(*payload)[16], static_cast<uint64_t>(query.startMs));
    put64(&(*payload)[24], static_cast<uint64_t>(query.endMs));
    put32(&(*payload)[32], static_cast<uint32_t>(query.calendarId.size()));
    appendText(payload, query.calendarId);
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeCalendarQuery(const std::vector<uint8_t> &payload,
                                CalendarQueryData *query) {
    CalendarQueryData decoded;
    size_t offset = 36;
    if (query == NULL || payload.size() < offset ||
        get16(&payload[0]) != CalendarQuery || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.kind = get32(&payload[4]);
    decoded.maxRecords = get32(&payload[8]);
    decoded.offset = get32(&payload[12]);
    decoded.startMs = static_cast<int64_t>(get64(&payload[16]));
    decoded.endMs = static_cast<int64_t>(get64(&payload[24]));
    if (!readText(payload, &offset, get32(&payload[32]), &decoded.calendarId) ||
        offset != payload.size() || !validCalendarQuery(decoded)) {
        return false;
    }
    *query = decoded;
    return true;
}

inline bool appendCalendar(std::vector<uint8_t> *payload,
                           const CalendarData &calendar) {
    const size_t base = payload->size();
    if (!validCalendar(calendar)) {
        return false;
    }
    payload->resize(base + 24, 0);
    put32(&(*payload)[base], calendar.flags);
    put32(&(*payload)[base + 4], calendar.colorArgb);
    put32(&(*payload)[base + 8], static_cast<uint32_t>(calendar.id.size()));
    put32(&(*payload)[base + 12], static_cast<uint32_t>(calendar.name.size()));
    put32(&(*payload)[base + 16], static_cast<uint32_t>(calendar.ownerName.size()));
    put32(&(*payload)[base + 20], static_cast<uint32_t>(calendar.ownerId.size()));
    appendText(payload, calendar.id);
    appendText(payload, calendar.name);
    appendText(payload, calendar.ownerName);
    appendText(payload, calendar.ownerId);
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool readCalendar(const std::vector<uint8_t> &payload, size_t *offset,
                         CalendarData *calendar) {
    CalendarData decoded;
    if (offset == NULL || calendar == NULL || *offset > payload.size() ||
        payload.size() - *offset < 24) {
        return false;
    }
    const size_t base = *offset;
    decoded.flags = get32(&payload[base]);
    decoded.colorArgb = get32(&payload[base + 4]);
    *offset += 24;
    if (!readText(payload, offset, get32(&payload[base + 8]), &decoded.id) ||
        !readText(payload, offset, get32(&payload[base + 12]), &decoded.name) ||
        !readText(payload, offset, get32(&payload[base + 16]), &decoded.ownerName) ||
        !readText(payload, offset, get32(&payload[base + 20]), &decoded.ownerId) ||
        !validCalendar(decoded)) {
        return false;
    }
    *calendar = decoded;
    return true;
}

inline bool appendCalendarEvent(std::vector<uint8_t> *payload,
                                const CalendarEventData &event) {
    const size_t base = payload->size();
    if (!validCalendarEvent(event)) {
        return false;
    }
    payload->resize(base + 60, 0);
    put32(&(*payload)[base], event.flags);
    put32(&(*payload)[base + 4], event.availability);
    put32(&(*payload)[base + 8], event.status);
    put32(&(*payload)[base + 12], static_cast<uint32_t>(event.attendees.size()));
    put32(&(*payload)[base + 16], static_cast<uint32_t>(event.reminderMinutes.size()));
    const std::string *strings[] = {
        &event.id, &event.calendarId, &event.baseEventId,
        &event.title, &event.description, &event.location,
    };
    for (size_t index = 0; index < 6; ++index) {
        put32(&(*payload)[base + 20 + index * 4],
              static_cast<uint32_t>(strings[index]->size()));
    }
    put64(&(*payload)[base + 44], static_cast<uint64_t>(event.startMs));
    put64(&(*payload)[base + 52], static_cast<uint64_t>(event.endMs));
    for (size_t index = 0; index < 6; ++index) {
        appendText(payload, *strings[index]);
    }
    for (size_t index = 0; index < event.attendees.size(); ++index) {
        const CalendarAttendeeData &attendee = event.attendees[index];
        const size_t attendeeBase = payload->size();
        payload->resize(attendeeBase + 20, 0);
        put32(&(*payload)[attendeeBase], attendee.flags);
        put32(&(*payload)[attendeeBase + 4], attendee.role);
        put32(&(*payload)[attendeeBase + 8], attendee.status);
        put32(&(*payload)[attendeeBase + 12], static_cast<uint32_t>(attendee.name.size()));
        put32(&(*payload)[attendeeBase + 16], static_cast<uint32_t>(attendee.email.size()));
        appendText(payload, attendee.name);
        appendText(payload, attendee.email);
    }
    for (size_t index = 0; index < event.reminderMinutes.size(); ++index) {
        const size_t reminderBase = payload->size();
        payload->resize(reminderBase + 4, 0);
        put32(&(*payload)[reminderBase],
              static_cast<uint32_t>(event.reminderMinutes[index]));
    }
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool readCalendarEvent(const std::vector<uint8_t> &payload,
                              size_t *offset, CalendarEventData *event) {
    CalendarEventData decoded;
    if (offset == NULL || event == NULL || *offset > payload.size() ||
        payload.size() - *offset < 60) {
        return false;
    }
    const size_t base = *offset;
    decoded.flags = get32(&payload[base]);
    decoded.availability = get32(&payload[base + 4]);
    decoded.status = get32(&payload[base + 8]);
    const uint32_t attendeeCount = get32(&payload[base + 12]);
    const uint32_t reminderCount = get32(&payload[base + 16]);
    decoded.startMs = static_cast<int64_t>(get64(&payload[base + 44]));
    decoded.endMs = static_cast<int64_t>(get64(&payload[base + 52]));
    *offset += 60;
    std::string *strings[] = {
        &decoded.id, &decoded.calendarId, &decoded.baseEventId,
        &decoded.title, &decoded.description, &decoded.location,
    };
    for (size_t index = 0; index < 6; ++index) {
        if (!readText(payload, offset, get32(&payload[base + 20 + index * 4]),
                      strings[index])) {
            return false;
        }
    }
    if (attendeeCount > kCalendarAttendeeMax ||
        reminderCount > kCalendarReminderMax) {
        return false;
    }
    for (uint32_t index = 0; index < attendeeCount; ++index) {
        if (*offset > payload.size() || payload.size() - *offset < 20) {
            return false;
        }
        const size_t attendeeBase = *offset;
        CalendarAttendeeData attendee;
        attendee.flags = get32(&payload[attendeeBase]);
        attendee.role = get32(&payload[attendeeBase + 4]);
        attendee.status = get32(&payload[attendeeBase + 8]);
        *offset += 20;
        if (!readText(payload, offset, get32(&payload[attendeeBase + 12]),
                      &attendee.name) ||
            !readText(payload, offset, get32(&payload[attendeeBase + 16]),
                      &attendee.email) || !validCalendarAttendee(attendee)) {
            return false;
        }
        decoded.attendees.push_back(attendee);
    }
    for (uint32_t index = 0; index < reminderCount; ++index) {
        if (*offset > payload.size() || payload.size() - *offset < 4) {
            return false;
        }
        decoded.reminderMinutes.push_back(
            static_cast<int32_t>(get32(&payload[*offset])));
        *offset += 4;
    }
    if (!validCalendarEvent(decoded)) {
        return false;
    }
    *event = decoded;
    return true;
}

inline bool encodeCalendarReply(uint32_t status,
                                const CalendarReplyData &reply,
                                std::vector<uint8_t> *payload) {
    if (payload == NULL || !validStatus(status) ||
        !validCalendarKind(reply.kind) || reply.nextOffset > kCalendarTotalMax ||
        (status != 0 && (reply.nextOffset != 0 || !reply.calendars.empty() ||
                         !reply.events.empty())) ||
        (reply.kind == CalendarQueryCalendars && !reply.events.empty()) ||
        (reply.kind == CalendarQueryEvents && !reply.calendars.empty())) {
        return false;
    }
    const size_t count = reply.kind == CalendarQueryCalendars ?
        reply.calendars.size() : reply.events.size();
    if (count > kCalendarPageMax) {
        return false;
    }
    payload->assign(20, 0);
    put16(&(*payload)[0], CalendarQuery);
    put32(&(*payload)[4], status);
    put32(&(*payload)[8], reply.kind);
    put32(&(*payload)[12], reply.nextOffset);
    put32(&(*payload)[16], static_cast<uint32_t>(count));
    for (size_t index = 0; index < count; ++index) {
        const bool encoded = reply.kind == CalendarQueryCalendars ?
            appendCalendar(payload, reply.calendars[index]) :
            appendCalendarEvent(payload, reply.events[index]);
        if (!encoded) {
            return false;
        }
    }
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeCalendarReply(const std::vector<uint8_t> &payload,
                                uint32_t *status, CalendarReplyData *reply) {
    CalendarReplyData decoded;
    if (status == NULL || reply == NULL || payload.size() < 20 ||
        get16(&payload[0]) != CalendarQuery || get16(&payload[2]) != 0 ||
        !validStatus(get32(&payload[4]))) {
        return false;
    }
    const uint32_t decodedStatus = get32(&payload[4]);
    decoded.kind = get32(&payload[8]);
    decoded.nextOffset = get32(&payload[12]);
    const uint32_t count = get32(&payload[16]);
    if (!validCalendarKind(decoded.kind) || count > kCalendarPageMax ||
        decoded.nextOffset > kCalendarTotalMax) {
        return false;
    }
    size_t offset = 20;
    for (uint32_t index = 0; index < count; ++index) {
        if (decoded.kind == CalendarQueryCalendars) {
            CalendarData calendar;
            if (!readCalendar(payload, &offset, &calendar)) return false;
            decoded.calendars.push_back(calendar);
        } else {
            CalendarEventData event;
            if (!readCalendarEvent(payload, &offset, &event)) return false;
            decoded.events.push_back(event);
        }
    }
    if (offset != payload.size() ||
        (decodedStatus != 0 && (count != 0 || decoded.nextOffset != 0))) {
        return false;
    }
    *status = decodedStatus;
    *reply = decoded;
    return true;
}

inline bool encodeCalendarChanged(std::vector<uint8_t> *payload) {
    if (payload == NULL) return false;
    payload->assign(4, 0);
    put16(&(*payload)[0], CalendarChanged);
    return true;
}

inline bool decodeCalendarChanged(const std::vector<uint8_t> &payload) {
    return payload.size() == 4 && get16(&payload[0]) == CalendarChanged &&
           get16(&payload[2]) == 0;
}

inline bool validContactKind(uint32_t kind) {
    return kind == ContactQueryList || kind == ContactQueryPhone;
}

inline bool validContactQuery(const ContactQueryData &query) {
    if (!validContactKind(query.kind) || query.maxRecords == 0 ||
        query.maxRecords > kContactPageMax || query.offset > kContactTotalMax ||
        !validText(query.query, kContactNumberMax)) {
        return false;
    }
    if (query.kind == ContactQueryList) {
        return query.query.empty();
    }
    return query.maxRecords == 1 && query.offset == 0 && !query.query.empty();
}

inline bool validContact(const ContactData &contact) {
    return contact.flags == 0 &&
           validText(contact.id, kContactIdMax, false) &&
           validText(contact.displayName, kContactNameMax, false) &&
           validText(contact.phoneNumber, kContactNumberMax) &&
           contact.avatar.size() <= kContactAvatarMax;
}

inline bool encodeContactQuery(const ContactQueryData &query,
                               std::vector<uint8_t> *payload) {
    if (payload == NULL || !validContactQuery(query)) return false;
    payload->assign(20, 0);
    put16(&(*payload)[0], ContactQuery);
    put32(&(*payload)[4], query.kind);
    put32(&(*payload)[8], query.maxRecords);
    put32(&(*payload)[12], query.offset);
    put32(&(*payload)[16], static_cast<uint32_t>(query.query.size()));
    appendText(payload, query.query);
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeContactQuery(const std::vector<uint8_t> &payload,
                               ContactQueryData *query) {
    ContactQueryData decoded;
    size_t offset = 20;
    if (query == NULL || payload.size() < offset ||
        get16(&payload[0]) != ContactQuery || get16(&payload[2]) != 0) {
        return false;
    }
    decoded.kind = get32(&payload[4]);
    decoded.maxRecords = get32(&payload[8]);
    decoded.offset = get32(&payload[12]);
    if (!readText(payload, &offset, get32(&payload[16]), &decoded.query) ||
        offset != payload.size() || !validContactQuery(decoded)) {
        return false;
    }
    *query = decoded;
    return true;
}

inline bool appendContact(std::vector<uint8_t> *payload,
                          const ContactData &contact) {
    const size_t base = payload->size();
    if (!validContact(contact)) return false;
    payload->resize(base + 20, 0);
    put32(&(*payload)[base], contact.flags);
    put32(&(*payload)[base + 4], static_cast<uint32_t>(contact.id.size()));
    put32(&(*payload)[base + 8], static_cast<uint32_t>(contact.displayName.size()));
    put32(&(*payload)[base + 12], static_cast<uint32_t>(contact.phoneNumber.size()));
    put32(&(*payload)[base + 16], static_cast<uint32_t>(contact.avatar.size()));
    appendText(payload, contact.id);
    appendText(payload, contact.displayName);
    appendText(payload, contact.phoneNumber);
    payload->insert(payload->end(), contact.avatar.begin(), contact.avatar.end());
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool readContact(const std::vector<uint8_t> &payload, size_t *offset,
                        ContactData *contact) {
    ContactData decoded;
    if (offset == NULL || contact == NULL || *offset > payload.size() ||
        payload.size() - *offset < 20) {
        return false;
    }
    const size_t base = *offset;
    decoded.flags = get32(&payload[base]);
    const uint32_t avatarSize = get32(&payload[base + 16]);
    *offset += 20;
    if (!readText(payload, offset, get32(&payload[base + 4]), &decoded.id) ||
        !readText(payload, offset, get32(&payload[base + 8]), &decoded.displayName) ||
        !readText(payload, offset, get32(&payload[base + 12]), &decoded.phoneNumber) ||
        avatarSize > kContactAvatarMax || *offset > payload.size() ||
        avatarSize > payload.size() - *offset) {
        return false;
    }
    decoded.avatar.assign(payload.begin() + *offset,
                          payload.begin() + *offset + avatarSize);
    *offset += avatarSize;
    if (!validContact(decoded)) return false;
    *contact = decoded;
    return true;
}

inline bool encodeContactReply(uint32_t status, const ContactReplyData &reply,
                               std::vector<uint8_t> *payload) {
    if (payload == NULL || !validStatus(status) ||
        !validContactKind(reply.kind) || reply.nextOffset > kContactTotalMax ||
        reply.contacts.size() > kContactPageMax ||
        (status != 0 && (reply.nextOffset != 0 || !reply.contacts.empty())) ||
        (reply.kind == ContactQueryPhone &&
         (reply.nextOffset != 0 || reply.contacts.size() > 1))) {
        return false;
    }
    payload->assign(20, 0);
    put16(&(*payload)[0], ContactQuery);
    put32(&(*payload)[4], status);
    put32(&(*payload)[8], reply.kind);
    put32(&(*payload)[12], reply.nextOffset);
    put32(&(*payload)[16], static_cast<uint32_t>(reply.contacts.size()));
    for (size_t index = 0; index < reply.contacts.size(); ++index) {
        if (!appendContact(payload, reply.contacts[index])) return false;
    }
    return payload->size() <= kMaxFrameSize - kHeaderSize;
}

inline bool decodeContactReply(const std::vector<uint8_t> &payload,
                               uint32_t *status, ContactReplyData *reply) {
    ContactReplyData decoded;
    if (status == NULL || reply == NULL || payload.size() < 20 ||
        get16(&payload[0]) != ContactQuery || get16(&payload[2]) != 0 ||
        !validStatus(get32(&payload[4]))) {
        return false;
    }
    const uint32_t decodedStatus = get32(&payload[4]);
    decoded.kind = get32(&payload[8]);
    decoded.nextOffset = get32(&payload[12]);
    const uint32_t count = get32(&payload[16]);
    if (!validContactKind(decoded.kind) || count > kContactPageMax ||
        decoded.nextOffset > kContactTotalMax ||
        (decoded.kind == ContactQueryPhone &&
         (decoded.nextOffset != 0 || count > 1))) {
        return false;
    }
    size_t offset = 20;
    for (uint32_t index = 0; index < count; ++index) {
        ContactData contact;
        if (!readContact(payload, &offset, &contact)) return false;
        decoded.contacts.push_back(contact);
    }
    if (offset != payload.size() ||
        (decodedStatus != 0 && (count != 0 || decoded.nextOffset != 0))) {
        return false;
    }
    *status = decodedStatus;
    *reply = decoded;
    return true;
}

inline bool encodeContactChanged(std::vector<uint8_t> *payload) {
    if (payload == NULL) return false;
    payload->assign(4, 0);
    put16(&(*payload)[0], ContactChanged);
    return true;
}

inline bool decodeContactChanged(const std::vector<uint8_t> &payload) {
    return payload.size() == 4 && get16(&payload[0]) == ContactChanged &&
           get16(&payload[2]) == 0;
}

inline bool waitReadable(int fd, int timeoutMs) {
    struct pollfd pollfd = { fd, POLLIN | POLLHUP, 0 };
    int result;
    do {
        result = poll(&pollfd, 1, timeoutMs);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (pollfd.revents & (POLLIN | POLLHUP));
}

inline bool encodeFrame(uint16_t type, uint64_t requestId,
                        const uint8_t *payload, size_t payloadSize,
                        std::vector<uint8_t> *bytes) {
    if (payloadSize > kMaxFrameSize - kHeaderSize ||
        (payloadSize != 0 && payload == NULL) || bytes == NULL) {
        return false;
    }
    bytes->resize(kHeaderSize + payloadSize);
    put32(&(*bytes)[0], static_cast<uint32_t>(bytes->size()));
    put16(&(*bytes)[4], kMajor);
    put16(&(*bytes)[6], kMinor);
    put16(&(*bytes)[8], type);
    put16(&(*bytes)[10], 0);
    put64(&(*bytes)[12], requestId);
    put32(&(*bytes)[20], static_cast<uint32_t>(payloadSize));
    if (payloadSize != 0) {
        memcpy(&(*bytes)[kHeaderSize], payload, payloadSize);
    }
    return true;
}

inline bool isEventFrame(const std::vector<uint8_t> &bytes,
                         uint16_t eventType) {
    return bytes.size() >= kHeaderSize + 2 &&
           get32(&bytes[0]) == bytes.size() &&
           get16(&bytes[8]) == Event && get64(&bytes[12]) == 0 &&
           get32(&bytes[20]) == bytes.size() - kHeaderSize &&
           get16(&bytes[kHeaderSize]) == eventType;
}

inline IoResult sendPacket(int fd, const std::vector<uint8_t> &bytes,
                           bool nonBlocking) {
    ssize_t written;
    int flags = MSG_NOSIGNAL | (nonBlocking ? MSG_DONTWAIT : 0);

    if (bytes.size() < kHeaderSize || bytes.size() > kMaxFrameSize) {
        return IoInvalid;
    }
    do {
        written = send(fd, &bytes[0], bytes.size(), flags);
    } while (written < 0 && errno == EINTR);
    if (written == static_cast<ssize_t>(bytes.size())) {
        return IoFrame;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return IoWouldBlock;
    }
    return IoError;
}

inline bool sendFrame(int fd, uint16_t type, uint64_t requestId,
                      const uint8_t *payload, size_t payloadSize) {
    std::vector<uint8_t> bytes;
    return encodeFrame(type, requestId, payload, payloadSize, &bytes) &&
           sendPacket(fd, bytes, false) == IoFrame;
}

inline IoResult receiveFrameResult(int fd, Frame *frame, bool nonBlocking) {
    uint8_t bytes[kMaxFrameSize];
    uint8_t control[CMSG_SPACE(sizeof(int) * 4)];
    struct iovec iov;
    struct msghdr message;
    ssize_t received;
    uint32_t totalLength;
    uint32_t payloadLength;
    bool hasControl = false;

    if (frame == NULL) {
        return IoInvalid;
    }

    memset(&message, 0, sizeof(message));
    memset(control, 0, sizeof(control));
    iov.iov_base = bytes;
    iov.iov_len = sizeof(bytes);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    do {
        received = recvmsg(fd, &message, MSG_CMSG_CLOEXEC | MSG_TRUNC |
                           (nonBlocking ? MSG_DONTWAIT : 0));
    } while (received < 0 && errno == EINTR);
    if (received < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? IoWouldBlock : IoError;
    }
    if (received == 0) {
        return IoClosed;
    }
    {
        struct cmsghdr *header;
        for (header = CMSG_FIRSTHDR(&message); header != NULL;
             header = CMSG_NXTHDR(&message, header)) {
            hasControl = true;
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                header->cmsg_len >= CMSG_LEN(sizeof(int))) {
                int *descriptors = reinterpret_cast<int *>(CMSG_DATA(header));
                size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                for (size_t index = 0; index < count; ++index) {
                    close(descriptors[index]);
                }
            }
        }
    }
    if (received < static_cast<ssize_t>(kHeaderSize) ||
        received > static_cast<ssize_t>(sizeof(bytes)) ||
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0 ||
        hasControl) {
        return IoInvalid;
    }
    totalLength = get32(&bytes[0]);
    payloadLength = get32(&bytes[20]);
    if (totalLength != static_cast<uint32_t>(received) ||
        totalLength != kHeaderSize + payloadLength ||
        get16(&bytes[4]) != kMajor || get16(&bytes[6]) > kMinor ||
        get16(&bytes[10]) != 0) {
        return IoInvalid;
    }
    frame->type = get16(&bytes[8]);
    frame->requestId = get64(&bytes[12]);
    frame->payload.assign(bytes + kHeaderSize, bytes + totalLength);
    return IoFrame;
}

inline bool receiveFrame(int fd, Frame *frame) {
    return receiveFrameResult(fd, frame, false) == IoFrame;
}

} // namespace lp3wire

#endif
