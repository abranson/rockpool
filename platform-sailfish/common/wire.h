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
static const uint16_t kMinor = 5;
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
};

enum EventType {
    TimeChanged = 1,
    NotificationPosted = 2,
    NotificationClosed = 3,
    CallChanged = 4,
    MediaVolumeChanged = 5,
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
};

struct NotificationCommandData {
    uint32_t command;
    std::string id;
};

struct MessageReplyData {
    std::string notificationId;
    std::string text;
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
static const size_t kCallIdMax = 128;
static const size_t kCallNameMax = 256;
static const size_t kCallNumberMax = 256;
static const uint32_t kLocationCoarse = 1;
static const uint32_t kLocationFine = 2;
static const uint32_t kLocationTimeoutMaxMs = 30000;

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
                               DomainMedia | DomainCalls | DomainLocation |
                               DomainTime;
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

inline bool validNotification(uint16_t eventType,
                              const NotificationData &notification) {
    if (!validNotificationId(notification.id)) {
        return false;
    }
    if (eventType == NotificationClosed) {
        return notification.flags == 0 && notification.timestampMs == 0 &&
               notification.closeReason <= 4 && notification.replacesId.empty() &&
               notification.applicationId.empty() &&
               notification.applicationName.empty() && notification.title.empty() &&
               notification.body.empty() && notification.category.empty() &&
               notification.iconName.empty();
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
        offset != payload.size() || !validNotification(decodedEvent, decoded)) {
        return false;
    }
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
                            operation != CallCommand &&
                            operation != MediaCommand) ||
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
