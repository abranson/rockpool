/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wire.h"

namespace {

void makePair(int pair[2]) {
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
}

void testValidFrame() {
    int pair[2];
    const uint8_t payload[] = { 1, 2, 3 };
    lp3wire::Frame frame;

    makePair(pair);
    assert(lp3wire::sendFrame(pair[0], lp3wire::Request, 42, payload, sizeof(payload)));
    assert(lp3wire::receiveFrame(pair[1], &frame));
    assert(frame.type == lp3wire::Request);
    assert(frame.requestId == 42);
    assert(frame.payload.size() == sizeof(payload));
    assert(memcmp(&frame.payload[0], payload, sizeof(payload)) == 0);
    close(pair[0]);
    close(pair[1]);
}

void testInvalidSendArguments() {
    int pair[2];

    makePair(pair);
    assert(!lp3wire::sendFrame(pair[0], lp3wire::Request, 1, NULL, 1));
    assert(!lp3wire::sendFrame(
        pair[0], lp3wire::Request, 1, NULL, lp3wire::kMaxFrameSize));
    close(pair[0]);
    close(pair[1]);
}

void testTimeGetCodec() {
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeTimeGet(&payload));
    assert(payload.size() == 4);
    assert(lp3wire::decodeTimeGet(payload));
    payload[2] = 1;
    assert(!lp3wire::decodeTimeGet(payload));
    payload.resize(3);
    assert(!lp3wire::decodeTimeGet(payload));
}

void testTimeReplyCodec() {
    const lp3wire::TimeState original = { -18000, INT64_C(1785678901234), 1 };
    lp3wire::TimeState decoded;
    uint32_t status = UINT32_MAX;
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeTimeReply(0, original, &payload));
    assert(payload.size() == 24);
    assert(lp3wire::decodeTimeReply(payload, &status, &decoded));
    assert(status == 0);
    assert(decoded.utcOffsetSeconds == original.utcOffsetSeconds);
    assert(decoded.unixMs == original.unixMs);
    assert(decoded.is24Hour == original.is24Hour);

    payload[2] = 1;
    assert(!lp3wire::decodeTimeReply(payload, &status, &decoded));
    payload[2] = 0;
    lp3wire::put32(&payload[20], 2);
    assert(!lp3wire::decodeTimeReply(payload, &status, &decoded));
    lp3wire::put32(&payload[20], 1);
    lp3wire::put32(&payload[8], 90000);
    assert(!lp3wire::decodeTimeReply(payload, &status, &decoded));
    lp3wire::put32(&payload[8], static_cast<uint32_t>(original.utcOffsetSeconds));
    lp3wire::put32(&payload[4], 99);
    assert(!lp3wire::decodeTimeReply(payload, &status, &decoded));
    payload.resize(23);
    assert(!lp3wire::decodeTimeReply(payload, &status, &decoded));
}

void testErrorReplyMustHaveZeroValues() {
    lp3wire::TimeState empty = { 0, 0, 0 };
    lp3wire::TimeState nonempty = { 3600, 0, 0 };
    lp3wire::TimeState decoded;
    uint32_t status;
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeTimeReply(4, empty, &payload));
    assert(lp3wire::decodeTimeReply(payload, &status, &decoded));
    assert(status == 4);
    assert(!lp3wire::encodeTimeReply(4, nonempty, &payload));
}

void testTimeChangedCodec() {
    const lp3wire::TimeState original = { 19800, INT64_C(-123456789), 0 };
    lp3wire::TimeState decoded;
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeTimeChanged(original, &payload));
    assert(payload.size() == 20);
    assert(lp3wire::decodeTimeChanged(payload, &decoded));
    assert(decoded.utcOffsetSeconds == original.utcOffsetSeconds);
    assert(decoded.unixMs == original.unixMs);
    assert(decoded.is24Hour == original.is24Hour);
    payload[0] = 2;
    assert(!lp3wire::decodeTimeChanged(payload, &decoded));
    payload[0] = lp3wire::TimeChanged;
    payload[3] = 1;
    assert(!lp3wire::decodeTimeChanged(payload, &decoded));
}

void testLocationCodec() {
    const lp3wire::LocationQueryData query = {
        lp3wire::kLocationFine,
        15000,
    };
    const lp3wire::LocationData location = {
        515074000,
        -1278000,
        12,
        INT64_C(1785678901234),
    };
    lp3wire::LocationQueryData decodedQuery;
    lp3wire::LocationData decodedLocation;
    uint32_t status = UINT32_MAX;
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeLocationQuery(query, &payload));
    assert(payload.size() == 12);
    assert(lp3wire::decodeLocationQuery(payload, &decodedQuery));
    assert(decodedQuery.accuracy == query.accuracy);
    assert(decodedQuery.timeoutMs == query.timeoutMs);
    lp3wire::put32(&payload[4], 0);
    assert(!lp3wire::decodeLocationQuery(payload, &decodedQuery));
    lp3wire::put32(&payload[4], lp3wire::kLocationCoarse);
    lp3wire::put32(&payload[8], 0);
    assert(!lp3wire::decodeLocationQuery(payload, &decodedQuery));
    lp3wire::put32(&payload[8], lp3wire::kLocationTimeoutMaxMs + 1);
    assert(!lp3wire::decodeLocationQuery(payload, &decodedQuery));
    lp3wire::put32(&payload[8], query.timeoutMs);
    payload[2] = 1;
    assert(!lp3wire::decodeLocationQuery(payload, &decodedQuery));
    payload[2] = 0;
    payload.resize(11);
    assert(!lp3wire::decodeLocationQuery(payload, &decodedQuery));

    assert(lp3wire::encodeLocationReply(0, location, &payload));
    assert(payload.size() == 28);
    assert(lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    assert(status == 0);
    assert(decodedLocation.latitudeE7 == location.latitudeE7);
    assert(decodedLocation.longitudeE7 == location.longitudeE7);
    assert(decodedLocation.accuracyM == location.accuracyM);
    assert(decodedLocation.timestampMs == location.timestampMs);
    lp3wire::put32(&payload[8], 900000001);
    assert(!lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    lp3wire::put32(&payload[8], static_cast<uint32_t>(location.latitudeE7));
    lp3wire::put32(&payload[12], 1800000001u);
    assert(!lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    lp3wire::put32(&payload[12], static_cast<uint32_t>(location.longitudeE7));
    lp3wire::put32(&payload[16], UINT32_MAX);
    assert(!lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    lp3wire::put32(&payload[16], static_cast<uint32_t>(location.accuracyM));
    lp3wire::put64(&payload[20], 0);
    assert(!lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    lp3wire::put64(&payload[20], static_cast<uint64_t>(location.timestampMs));
    payload[2] = 1;
    assert(!lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    payload[2] = 0;
    payload.resize(27);
    assert(!lp3wire::decodeLocationReply(payload, &status, &decodedLocation));

    lp3wire::LocationData empty = {};
    assert(lp3wire::encodeLocationReply(5, empty, &payload));
    assert(lp3wire::decodeLocationReply(payload, &status, &decodedLocation));
    assert(status == 5);
    assert(lp3wire::emptyLocation(decodedLocation));
    assert(!lp3wire::encodeLocationReply(5, location, &payload));
    assert(!lp3wire::encodeLocationReply(9, empty, &payload));
}

void testNotificationCodec() {
    lp3wire::NotificationData original;
    lp3wire::NotificationData decoded;
    uint16_t eventType = 0;
    std::vector<uint8_t> payload;

    original.flags = lp3wire::NotificationHasDefaultAction |
        lp3wire::NotificationHasReplyAction;
    original.timestampMs = INT64_C(1785678901234);
    original.closeReason = 0;
    original.id = "42";
    original.replacesId = "41";
    original.applicationId = "org.example.messages";
    original.applicationName = "Messages";
    original.title = "Alice";
    original.body = "Hello \xf0\x9f\x91\x8b";
    original.category = "x-nemo.messaging.im";
    original.iconName = "icon-lock-sms";

    assert(lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, original, &payload));
    assert(lp3wire::decodeNotificationEvent(payload, &eventType, &decoded));
    assert(eventType == lp3wire::NotificationPosted);
    assert(decoded.flags == original.flags);
    assert(decoded.timestampMs == original.timestampMs);
    assert(decoded.id == original.id);
    assert(decoded.replacesId == original.replacesId);
    assert(decoded.applicationId == original.applicationId);
    assert(decoded.applicationName == original.applicationName);
    assert(decoded.title == original.title);
    assert(decoded.body == original.body);
    assert(decoded.category == original.category);
    assert(decoded.iconName == original.iconName);

    payload[2] = 1;
    assert(!lp3wire::decodeNotificationEvent(payload, &eventType, &decoded));
    payload[2] = 0;
    lp3wire::put32(&payload[4], 0x80000000u);
    assert(!lp3wire::decodeNotificationEvent(payload, &eventType, &decoded));

    original.id = "not-a-number";
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, original, &payload));
    original.id = "4294967296";
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, original, &payload));
    original.id = "42";
    original.replacesId = "invalid";
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, original, &payload));
}

void testNotificationClosedCodec() {
    lp3wire::NotificationData original;
    lp3wire::NotificationData decoded;
    uint16_t eventType = 0;
    std::vector<uint8_t> payload;

    original.flags = 0;
    original.timestampMs = 0;
    original.closeReason = 2;
    original.id = "42";
    assert(lp3wire::encodeNotificationEvent(
        lp3wire::NotificationClosed, original, &payload));
    assert(lp3wire::decodeNotificationEvent(payload, &eventType, &decoded));
    assert(eventType == lp3wire::NotificationClosed);
    assert(decoded.id == "42");
    assert(decoded.closeReason == 2);

    original.title = "not allowed";
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationClosed, original, &payload));
    original.title.clear();
    original.closeReason = 5;
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationClosed, original, &payload));
}

void testNotificationRejectsInvalidTextAndLengths() {
    lp3wire::NotificationData notification = {};
    lp3wire::NotificationData decoded;
    uint16_t eventType;
    std::vector<uint8_t> payload;

    notification.flags = 0;
    notification.timestampMs = 1;
    notification.closeReason = 0;
    notification.id = "1";
    notification.applicationId = "example";
    notification.title = "title";
    assert(lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, notification, &payload));

    notification.body.assign("bad\xc0\x80", 5);
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, notification, &payload));
    notification.body.clear();
    notification.title.assign(lp3wire::kNotificationTitleMax + 1, 'x');
    assert(!lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, notification, &payload));

    notification.title = "title";
    assert(lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, notification, &payload));
    lp3wire::put32(&payload[20], UINT32_MAX);
    assert(!lp3wire::decodeNotificationEvent(payload, &eventType, &decoded));
}

void testNotificationCommandCodec() {
    lp3wire::NotificationCommandData original;
    lp3wire::NotificationCommandData decoded;
    uint32_t status = UINT32_MAX;
    std::vector<uint8_t> payload;

    original.command = lp3wire::NotificationDismiss;
    original.id = "42";
    assert(lp3wire::encodeNotificationCommand(original, &payload));
    assert(lp3wire::decodeNotificationCommand(payload, &decoded));
    assert(decoded.command == original.command);
    assert(decoded.id == original.id);

    original.command = 99;
    assert(!lp3wire::encodeNotificationCommand(original, &payload));
    original.command = lp3wire::NotificationDismiss;
    original.id = "0";
    assert(!lp3wire::encodeNotificationCommand(original, &payload));
    original.id = "invalid";
    assert(!lp3wire::encodeNotificationCommand(original, &payload));

    assert(lp3wire::encodeStatusReply(
        lp3wire::NotificationCommand, 0, &payload));
    assert(lp3wire::decodeStatusReply(
        payload, lp3wire::NotificationCommand, &status));
    assert(status == 0);
    payload[2] = 1;
    assert(!lp3wire::decodeStatusReply(
        payload, lp3wire::NotificationCommand, &status));
}

void testMessageReplyCodec() {
    lp3wire::MessageReplyData original;
    lp3wire::MessageReplyData decoded;
    uint32_t status = UINT32_MAX;
    std::vector<uint8_t> payload;

    original.notificationId = "4294967295";
    original.text = "Hello \xf0\x9f\x91\x8b";
    assert(lp3wire::encodeMessageReply(original, &payload));
    assert(lp3wire::decodeMessageReply(payload, &decoded));
    assert(decoded.notificationId == original.notificationId);
    assert(decoded.text == original.text);

    original.notificationId = "0";
    assert(!lp3wire::encodeMessageReply(original, &payload));
    original.notificationId = "042";
    assert(!lp3wire::encodeMessageReply(original, &payload));
    original.notificationId = "42";
    original.text.clear();
    assert(!lp3wire::encodeMessageReply(original, &payload));
    original.text.assign(lp3wire::kMessageTextMax, 'x');
    assert(lp3wire::encodeMessageReply(original, &payload));
    original.text.push_back('x');
    assert(!lp3wire::encodeMessageReply(original, &payload));
    original.text.assign("bad\xc0\x80", 5);
    assert(!lp3wire::encodeMessageReply(original, &payload));

    original.text = "reply";
    assert(lp3wire::encodeMessageReply(original, &payload));
    payload.push_back('x');
    assert(!lp3wire::decodeMessageReply(payload, &decoded));
    assert(lp3wire::encodeMessageReply(original, &payload));
    payload[2] = 1;
    assert(!lp3wire::decodeMessageReply(payload, &decoded));
    payload[2] = 0;
    lp3wire::put32(&payload[8], UINT32_MAX);
    assert(!lp3wire::decodeMessageReply(payload, &decoded));

    assert(lp3wire::encodeStatusReply(lp3wire::MessageReply, 0, &payload));
    assert(lp3wire::decodeStatusReply(
        payload, lp3wire::MessageReply, &status));
    assert(status == 0);
    assert(!lp3wire::decodeStatusReply(
        payload, lp3wire::NotificationCommand, &status));
}

void testCallChangedCodec() {
    const uint32_t states[] = {
        lp3wire::CallRinging,
        lp3wire::CallDialing,
        lp3wire::CallActive,
        lp3wire::CallHeld,
    };
    lp3wire::CallData original;
    lp3wire::CallData decoded;
    std::vector<uint8_t> payload;

    original.id = "call_42";
    original.name = "Alice \xf0\x9f\x91\x8b";
    original.number = "+123456789";
    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        original.state = states[index];
        assert(lp3wire::encodeCallChanged(original, &payload));
        assert(payload.size() == 20 + original.id.size() + original.name.size() +
               original.number.size());
        assert(lp3wire::decodeCallChanged(payload, &decoded));
        assert(decoded.state == original.state);
        assert(decoded.id == original.id);
        assert(decoded.name == original.name);
        assert(decoded.number == original.number);
    }

    original.state = lp3wire::CallEnded;
    original.name.clear();
    original.number.clear();
    assert(lp3wire::encodeCallChanged(original, &payload));
    assert(lp3wire::decodeCallChanged(payload, &decoded));
    assert(decoded.state == lp3wire::CallEnded);
    assert(decoded.id == original.id);
    assert(decoded.name.empty());
    assert(decoded.number.empty());
}

void testCallChangedRejectsMalformedData() {
    lp3wire::CallData call;
    lp3wire::CallData decoded;
    std::vector<uint8_t> payload;

    call.state = lp3wire::CallActive;
    call.id = "call_42";
    call.name = "Alice";
    call.number = "+123";
    assert(lp3wire::encodeCallChanged(call, &payload));

    lp3wire::put32(&payload[4], lp3wire::CallHeld + 1);
    assert(!lp3wire::decodeCallChanged(payload, &decoded));
    lp3wire::put32(&payload[4], lp3wire::CallActive);
    payload[2] = 1;
    assert(!lp3wire::decodeCallChanged(payload, &decoded));
    payload[2] = 0;
    lp3wire::put32(&payload[8], UINT32_MAX);
    assert(!lp3wire::decodeCallChanged(payload, &decoded));
    lp3wire::put32(&payload[8], static_cast<uint32_t>(call.id.size()));
    payload.push_back(0);
    assert(!lp3wire::decodeCallChanged(payload, &decoded));
    payload.pop_back();
    payload[20 + call.id.size()] = 0;
    assert(!lp3wire::decodeCallChanged(payload, &decoded));
    payload[20 + call.id.size()] = 'A';
    payload[20 + call.id.size()] = 0xc0;
    assert(!lp3wire::decodeCallChanged(payload, &decoded));
    payload[20 + call.id.size()] = 'A';

    call.name.assign("bad\xc0\x80", 5);
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.name.assign("bad\0text", 8);
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.name.assign(lp3wire::kCallNameMax + 1, 'x');
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.name = "Alice";
    call.number.assign(lp3wire::kCallNumberMax + 1, 'x');
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.number = "+123";
    call.id.assign(lp3wire::kCallIdMax + 1, 'a');
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.id = "call-42";
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.id = "call/42";
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.id = "call_42";
    call.state = lp3wire::CallEnded;
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.name.clear();
    call.number.clear();
    call.id.clear();
    assert(!lp3wire::encodeCallChanged(call, &payload));
    call.state = lp3wire::CallActive;
    assert(!lp3wire::encodeCallChanged(call, &payload));
}

void testCallCommandCodec() {
    lp3wire::CallCommandData original;
    lp3wire::CallCommandData decoded;
    uint32_t status = UINT32_MAX;
    std::vector<uint8_t> payload;

    original.command = lp3wire::CallAnswer;
    original.id = "call_42";
    assert(lp3wire::encodeCallCommand(original, &payload));
    assert(payload.size() == 12 + original.id.size());
    assert(lp3wire::decodeCallCommand(payload, &decoded));
    assert(decoded.command == original.command);
    assert(decoded.id == original.id);

    original.command = lp3wire::CallHangUp;
    assert(lp3wire::encodeCallCommand(original, &payload));
    assert(lp3wire::decodeCallCommand(payload, &decoded));
    original.command = lp3wire::CallSilence;
    original.id.clear();
    assert(lp3wire::encodeCallCommand(original, &payload));
    assert(lp3wire::decodeCallCommand(payload, &decoded));

    original.id = "call_42";
    assert(!lp3wire::encodeCallCommand(original, &payload));
    original.command = lp3wire::CallAnswer;
    original.id.clear();
    assert(!lp3wire::encodeCallCommand(original, &payload));
    original.id = "bad-id";
    assert(!lp3wire::encodeCallCommand(original, &payload));
    original.id = "call_42";
    original.command = 99;
    assert(!lp3wire::encodeCallCommand(original, &payload));

    original.command = lp3wire::CallAnswer;
    assert(lp3wire::encodeCallCommand(original, &payload));
    payload[2] = 1;
    assert(!lp3wire::decodeCallCommand(payload, &decoded));
    payload[2] = 0;
    payload.push_back(0);
    assert(!lp3wire::decodeCallCommand(payload, &decoded));

    assert(lp3wire::encodeStatusReply(lp3wire::CallCommand, 0, &payload));
    assert(lp3wire::decodeStatusReply(payload, lp3wire::CallCommand, &status));
    assert(status == 0);
}

void testMediaVolumeCodec() {
    const lp3wire::MediaVolumeData original = {
        lp3wire::MediaSystemVolume,
        73,
    };
    lp3wire::MediaVolumeData decoded;
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeMediaVolumeChanged(original, &payload));
    assert(payload.size() == 12);
    assert(lp3wire::decodeMediaVolumeChanged(payload, &decoded));
    assert(decoded.flags == original.flags);
    assert(decoded.volumePercent == original.volumePercent);

    lp3wire::put32(&payload[4], 0);
    assert(!lp3wire::decodeMediaVolumeChanged(payload, &decoded));
    lp3wire::put32(&payload[4], lp3wire::MediaSystemVolume | (1u << 1));
    assert(!lp3wire::decodeMediaVolumeChanged(payload, &decoded));
    lp3wire::put32(&payload[4], lp3wire::MediaSystemVolume);
    lp3wire::put32(&payload[8], 101);
    assert(!lp3wire::decodeMediaVolumeChanged(payload, &decoded));
    lp3wire::put32(&payload[8], UINT32_MAX);
    assert(!lp3wire::decodeMediaVolumeChanged(payload, &decoded));
    lp3wire::put32(&payload[8], static_cast<uint32_t>(original.volumePercent));
    payload[2] = 1;
    assert(!lp3wire::decodeMediaVolumeChanged(payload, &decoded));
    payload[2] = 0;
    payload.resize(11);
    assert(!lp3wire::decodeMediaVolumeChanged(payload, &decoded));
}

void testMediaCommandCodec() {
    const uint32_t commands[] = {
        lp3wire::MediaVolumeUp,
        lp3wire::MediaVolumeDown,
    };
    lp3wire::MediaCommandData original;
    lp3wire::MediaCommandData decoded;
    uint32_t status = UINT32_MAX;
    std::vector<uint8_t> payload;

    for (size_t index = 0; index < sizeof(commands) / sizeof(commands[0]); ++index) {
        original.command = commands[index];
        assert(lp3wire::encodeMediaCommand(original, &payload));
        assert(payload.size() == 8);
        assert(lp3wire::decodeMediaCommand(payload, &decoded));
        assert(decoded.command == original.command);
    }

    original.command = 1; // Generic MPRIS play/pause must not reach provider.
    assert(!lp3wire::encodeMediaCommand(original, &payload));
    original.command = 2; // Generic MPRIS next must not reach provider.
    assert(!lp3wire::encodeMediaCommand(original, &payload));
    original.command = 3; // Generic MPRIS previous must not reach provider.
    assert(!lp3wire::encodeMediaCommand(original, &payload));
    original.command = 99;
    assert(!lp3wire::encodeMediaCommand(original, &payload));

    original.command = lp3wire::MediaVolumeUp;
    assert(lp3wire::encodeMediaCommand(original, &payload));
    lp3wire::put32(&payload[4], 99);
    assert(!lp3wire::decodeMediaCommand(payload, &decoded));
    lp3wire::put32(&payload[4], lp3wire::MediaVolumeDown);
    payload[2] = 1;
    assert(!lp3wire::decodeMediaCommand(payload, &decoded));
    payload[2] = 0;
    payload.push_back(0);
    assert(!lp3wire::decodeMediaCommand(payload, &decoded));

    assert(lp3wire::encodeStatusReply(lp3wire::MediaCommand, 8, &payload));
    assert(lp3wire::decodeStatusReply(
        payload, lp3wire::MediaCommand, &status));
    assert(status == 8);
    lp3wire::put32(&payload[4], 9);
    assert(!lp3wire::decodeStatusReply(
        payload, lp3wire::MediaCommand, &status));
    assert(!lp3wire::decodeStatusReply(
        payload, lp3wire::CallCommand, &status));
}

void testEventFrameClassification() {
    lp3wire::TimeState time = { 0, 1, 1 };
    lp3wire::NotificationData notification = {};
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;

    assert(lp3wire::encodeTimeChanged(time, &payload));
    assert(lp3wire::encodeFrame(
        lp3wire::Event, 0, &payload[0], payload.size(), &frame));
    assert(lp3wire::isEventFrame(frame, lp3wire::TimeChanged));
    assert(!lp3wire::isEventFrame(frame, lp3wire::NotificationPosted));

    notification.id = "42";
    notification.applicationId = "org.example.app";
    notification.title = "Title";
    assert(lp3wire::encodeNotificationEvent(
        lp3wire::NotificationPosted, notification, &payload));
    assert(lp3wire::encodeFrame(
        lp3wire::Event, 0, &payload[0], payload.size(), &frame));
    assert(lp3wire::isEventFrame(frame, lp3wire::NotificationPosted));
    assert(!lp3wire::isEventFrame(frame, lp3wire::TimeChanged));

    lp3wire::CallData call;
    call.state = lp3wire::CallRinging;
    call.id = "call_42";
    assert(lp3wire::encodeCallChanged(call, &payload));
    assert(lp3wire::encodeFrame(
        lp3wire::Event, 0, &payload[0], payload.size(), &frame));
    assert(lp3wire::isEventFrame(frame, lp3wire::CallChanged));
    assert(!lp3wire::isEventFrame(frame, lp3wire::NotificationPosted));

    lp3wire::MediaVolumeData volume = {
        lp3wire::MediaSystemVolume,
        50,
    };
    assert(lp3wire::encodeMediaVolumeChanged(volume, &payload));
    assert(lp3wire::encodeFrame(
        lp3wire::Event, 0, &payload[0], payload.size(), &frame));
    assert(lp3wire::isEventFrame(frame, lp3wire::MediaVolumeChanged));
    assert(!lp3wire::isEventFrame(frame, lp3wire::CallChanged));
}

void testHealthCodec() {
    const lp3wire::HealthState original = {
        lp3wire::DomainCalls | lp3wire::DomainMessaging,
        lp3wire::DomainNotifications | lp3wire::DomainMedia,
        lp3wire::DomainTime | lp3wire::DomainLocation,
    };
    lp3wire::HealthState decoded;
    std::vector<uint8_t> payload;

    assert(lp3wire::encodeHealth(original, &payload));
    assert(lp3wire::decodeHealth(payload, &decoded));
    assert(decoded.readyDomains == original.readyDomains);
    assert(decoded.degradedDomains == original.degradedDomains);
    assert(decoded.failedDomains == original.failedDomains);
    lp3wire::put64(&payload[8], lp3wire::DomainTime);
    assert(!lp3wire::decodeHealth(payload, &decoded));
    lp3wire::put64(&payload[8], lp3wire::DomainNotifications);
    assert(!lp3wire::decodeHealth(payload, &decoded));
    lp3wire::put64(&payload[8],
                    lp3wire::DomainNotifications | lp3wire::DomainMedia);
    lp3wire::put64(&payload[16], UINT64_C(1) << 63);
    assert(!lp3wire::decodeHealth(payload, &decoded));
}

void testNonBlockingReceive() {
    int pair[2];
    lp3wire::Frame frame;

    makePair(pair);
    assert(lp3wire::receiveFrameResult(pair[0], &frame, true) ==
           lp3wire::IoWouldBlock);
    close(pair[0]);
    assert(lp3wire::receiveFrameResult(pair[1], &frame, true) ==
           lp3wire::IoClosed);
    close(pair[1]);
}

void testMalformedLength() {
    int pair[2];
    uint8_t bytes[lp3wire::kHeaderSize];
    lp3wire::Frame frame;

    makePair(pair);
    memset(bytes, 0, sizeof(bytes));
    lp3wire::put32(&bytes[0], static_cast<uint32_t>(sizeof(bytes) - 1));
    lp3wire::put16(&bytes[4], lp3wire::kMajor);
    lp3wire::put16(&bytes[6], lp3wire::kMinor);
    lp3wire::put16(&bytes[8], lp3wire::Hello);
    assert(send(pair[0], bytes, sizeof(bytes), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(sizeof(bytes)));
    assert(!lp3wire::receiveFrame(pair[1], &frame));
    close(pair[0]);
    close(pair[1]);
}

void testRejectsPassedDescriptors() {
    int pair[2];
    int passed = -1;
    uint8_t bytes[lp3wire::kHeaderSize];
    uint8_t control[CMSG_SPACE(sizeof(passed))];
    struct iovec iov;
    struct msghdr message;
    struct cmsghdr *header;
    lp3wire::Frame frame;

    makePair(pair);
    passed = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(passed >= 0);
    memset(bytes, 0, sizeof(bytes));
    lp3wire::put32(&bytes[0], sizeof(bytes));
    lp3wire::put16(&bytes[4], lp3wire::kMajor);
    lp3wire::put16(&bytes[6], lp3wire::kMinor);
    lp3wire::put16(&bytes[8], lp3wire::Hello);
    memset(&message, 0, sizeof(message));
    iov.iov_base = bytes;
    iov.iov_len = sizeof(bytes);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    header = CMSG_FIRSTHDR(&message);
    assert(header != NULL);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(passed));
    memcpy(CMSG_DATA(header), &passed, sizeof(passed));
    assert(sendmsg(pair[0], &message, MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(bytes)));
    assert(!lp3wire::receiveFrame(pair[1], &frame));
    close(passed);
    close(pair[0]);
    close(pair[1]);
}

} // namespace

int main() {
    testValidFrame();
    testInvalidSendArguments();
    testTimeGetCodec();
    testTimeReplyCodec();
    testErrorReplyMustHaveZeroValues();
    testTimeChangedCodec();
    testLocationCodec();
    testNotificationCodec();
    testNotificationClosedCodec();
    testNotificationRejectsInvalidTextAndLengths();
    testNotificationCommandCodec();
    testMessageReplyCodec();
    testCallChangedCodec();
    testCallChangedRejectsMalformedData();
    testCallCommandCodec();
    testMediaVolumeCodec();
    testMediaCommandCodec();
    testEventFrameClassification();
    testHealthCodec();
    testNonBlockingReceive();
    testMalformedLength();
    testRejectsPassedDescriptors();
    return 0;
}
