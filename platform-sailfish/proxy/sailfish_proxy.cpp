/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Non-Qt libpebble3d Sailfish provider proxy.  The helper owns every Qt and
 * Sailfish API; this DSO validates the ABI and relays a small typed protocol.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "libpebble3d-launcher-wire.h"
#include "libpebble3d-platform.h"
#include "wire.h"

#ifndef LP3_PLATFORM_BUILD_ID
#define LP3_PLATFORM_BUILD_ID "sailfish-provider-v1"
#endif

/* Providers complete the ABI's intentionally opaque instance type. */
struct lp3_platform_instance {};

namespace {

const char kProviderName[] = "sailfish";
const char kBuildId[] = LP3_PLATFORM_BUILD_ID;
const char kPrivilegedGroup[] = "privileged";
const unsigned int kRestartDelays[] = { 1, 5, 30 };
const unsigned int kMaximumFailures = 3;
const time_t kFailureWindowSeconds = 10 * 60;
const size_t kMaximumOutstanding = 64;
const size_t kMaximumOutgoing = 64;
const std::chrono::seconds kRequestTimeout(2);
const std::chrono::seconds kBondRemovalTimeout(5);
const std::chrono::seconds kCancellationGrace(2);
const uint64_t kSupportedDomains = LP3_PLATFORM_DOMAIN_TIME |
    LP3_PLATFORM_DOMAIN_NOTIFICATIONS | LP3_PLATFORM_DOMAIN_MESSAGING |
    LP3_PLATFORM_DOMAIN_MEDIA |
    LP3_PLATFORM_DOMAIN_CALLS | LP3_PLATFORM_DOMAIN_CALENDAR |
    LP3_PLATFORM_DOMAIN_CONTACTS |
    LP3_PLATFORM_DOMAIN_LOCATION;

struct Pending {
    std::condition_variable condition;
    uint16_t operation;
    bool complete;
    int32_t status;
    lp3wire::TimeState time;
    lp3wire::LocationData location;
    lp3wire::CalendarReplyData calendar;
    lp3wire::ContactReplyData contacts;

    explicit Pending(uint16_t requestOperation)
        : operation(requestOperation), complete(false),
          status(LP3_PLATFORM_UNAVAILABLE) {
        memset(&time, 0, sizeof(time));
        memset(&location, 0, sizeof(location));
        calendar.kind = lp3wire::CalendarQueryCalendars;
        calendar.nextOffset = 0;
        contacts.kind = lp3wire::ContactQueryList;
        contacts.nextOffset = 0;
    }
};

struct Tombstone {
    uint16_t operation;
    std::chrono::steady_clock::time_point expires;
};

struct SailfishInstance : lp3_platform_instance {
    std::mutex mutex;
    std::thread worker;
    std::atomic<bool> stopping;
    int control;
    int wake;
    int socket;
    pid_t helperPid;
    bool started;
    bool latched;
    bool forceReset;
    uint64_t nextRequestId;
    uint64_t readyDomains;
    uint64_t degradedDomains;
    uint64_t failedDomains;
    lp3_platform_event_callback event;
    void *eventContext;
    std::deque<std::vector<uint8_t> > outgoing;
    std::map<uint64_t, std::shared_ptr<Pending> > pending;
    std::map<uint64_t, Tombstone> tombstones;

    SailfishInstance()
        : stopping(false), control(-1), wake(-1), socket(-1), helperPid(-1),
          started(false), latched(false), forceReset(false),
          nextRequestId(UINT64_C(1) << 63), readyDomains(0),
          degradedDomains(0), failedDomains(0),
          event(NULL), eventContext(NULL) {
    }
};

void wakeWorker(SailfishInstance *instance) {
    uint64_t value = 1;
    ssize_t written;

    if (instance->wake < 0) {
        return;
    }
    do {
        written = write(instance->wake, &value, sizeof(value));
    } while (written < 0 && errno == EINTR);
}

void drainWake(int fd) {
    uint64_t value;
    ssize_t received;
    do {
        received = read(fd, &value, sizeof(value));
    } while (received == static_cast<ssize_t>(sizeof(value)) ||
             (received < 0 && errno == EINTR));
}

bool validateLauncherSocket(int fd) {
    int type = 0;
    socklen_t typeLength = sizeof(type);
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    struct group *privileged = getgrnam(kPrivilegedGroup);

    if (privileged == NULL) {
        return false;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &typeLength) != 0 ||
        type != SOCK_SEQPACKET ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        return false;
    }
    return credentials.pid == getppid() && credentials.uid == getuid() &&
        credentials.gid == privileged->gr_gid;
}

bool sendLauncherCommand(int fd, uint16_t type) {
    lp3_launcher_message_v1 message;
    ssize_t written;

    memset(&message, 0, sizeof(message));
    message.magic = LP3_LAUNCHER_MAGIC;
    message.version = LP3_LAUNCHER_VERSION;
    message.type = type;
    do {
        written = send(fd, &message, sizeof(message), MSG_NOSIGNAL);
    } while (written < 0 && errno == EINTR);
    return written == static_cast<ssize_t>(sizeof(message));
}

bool waitReadableOrStopping(SailfishInstance *instance, int fd, int timeoutMs) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!instance->stopping.load()) {
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count());
        struct pollfd descriptors[2] = {
            { fd, POLLIN | POLLHUP, 0 },
            { instance->wake, POLLIN, 0 },
        };
        int result;
        do {
            result = poll(descriptors, 2, remaining > 0 ? remaining : 1);
        } while (result < 0 && errno == EINTR && !instance->stopping.load());
        if (instance->stopping.load() || result <= 0) {
            return false;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            drainWake(instance->wake);
            if (instance->stopping.load()) {
                return false;
            }
        }
        if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
            return true;
        }
    }
    return false;
}

bool waitReadableForLauncherStop(int fd, int timeoutMs) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count());
        struct pollfd descriptor = { fd, POLLIN | POLLHUP, 0 };
        int result;
        do {
            result = poll(&descriptor, 1, remaining > 0 ? remaining : 1);
        } while (result < 0 && errno == EINTR);
        if (result <= 0) {
            return false;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
            return true;
        }
    }
}

bool receiveLauncherReply(SailfishInstance *instance, int fd,
                          lp3_launcher_message_v1 *reply,
                          int *receivedFd, int timeoutMs,
                          bool allowWhileStopping) {
    uint8_t control[CMSG_SPACE(sizeof(int) * 4)];
    struct iovec iov;
    struct msghdr message;
    struct cmsghdr *header;
    ssize_t received;
    unsigned int descriptorCount = 0;
    bool unexpectedControl = false;

    *receivedFd = -1;
    if (!(allowWhileStopping ? waitReadableForLauncherStop(fd, timeoutMs) :
          waitReadableOrStopping(instance, fd, timeoutMs))) {
        return false;
    }
    memset(&message, 0, sizeof(message));
    memset(control, 0, sizeof(control));
    iov.iov_base = reply;
    iov.iov_len = sizeof(*reply);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    do {
        received = recvmsg(fd, &message, MSG_CMSG_CLOEXEC | MSG_TRUNC);
    } while (received < 0 && errno == EINTR);
    for (header = CMSG_FIRSTHDR(&message); header != NULL;
         header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
            header->cmsg_len >= CMSG_LEN(sizeof(int))) {
            int *descriptors = reinterpret_cast<int *>(CMSG_DATA(header));
            const size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t index = 0; index < count; ++index) {
                if (descriptorCount == 0) {
                    *receivedFd = descriptors[index];
                } else {
                    close(descriptors[index]);
                }
                ++descriptorCount;
            }
        } else {
            unexpectedControl = true;
        }
    }
    if (received != static_cast<ssize_t>(sizeof(*reply)) ||
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0 ||
        reply->magic != LP3_LAUNCHER_MAGIC ||
        reply->version != LP3_LAUNCHER_VERSION || descriptorCount > 1 ||
        unexpectedControl) {
        if (*receivedFd >= 0) {
            close(*receivedFd);
            *receivedFd = -1;
        }
        return false;
    }
    return true;
}

bool receiveExpectedLauncherReply(
    SailfishInstance *instance, int fd, uint16_t expectedType,
    lp3_launcher_message_v1 *reply, int *receivedFd, int timeoutMs,
    bool allowWhileStopping) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count());
        if (!receiveLauncherReply(instance, fd, reply, receivedFd,
                                  remaining > 0 ? remaining : 1,
                                  allowWhileStopping)) {
            return false;
        }
        if (reply->type == expectedType) {
            return true;
        }
        if (*receivedFd >= 0) {
            close(*receivedFd);
            *receivedFd = -1;
        }
        /*
         * The control protocol is serialized but predates request IDs. A
         * bounded timeout can leave the opposite reply in the persistent
         * launcher socket, so consume only that one known stale shape.
         */
        if (!((expectedType == LP3_LAUNCHER_HOST_STARTED &&
               reply->type == LP3_LAUNCHER_HOST_STOPPED) ||
              (expectedType == LP3_LAUNCHER_HOST_STOPPED &&
               reply->type == LP3_LAUNCHER_HOST_STARTED))) {
            return false;
        }
    }
    return false;
}

void publishDisconnected(SailfishInstance *instance) {
    lp3_platform_event_callback callback = NULL;
    void *context = NULL;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(instance->mutex);
        changed = instance->socket >= 0 || instance->helperPid > 0 ||
            instance->readyDomains != 0 || instance->degradedDomains != 0 ||
            instance->failedDomains != 0;
        instance->socket = -1;
        instance->helperPid = -1;
        instance->readyDomains = 0;
        instance->degradedDomains = 0;
        instance->failedDomains = 0;
        callback = instance->event;
        context = instance->eventContext;
    }
    if (changed && callback != NULL) {
        static const char error[] = "platform helper disconnected";
        lp3_platform_provider_status_v1 status;
        lp3_platform_event_v1 event;
        memset(&status, 0, sizeof(status));
        status.struct_size = sizeof(status);
        status.state = LP3_PLATFORM_PROVIDER_DEGRADED;
        status.degraded_domains = kSupportedDomains;
        status.error.data = error;
        status.error.size = sizeof(error) - 1;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        event.type = LP3_PLATFORM_EVENT_PROVIDER_STATUS;
        event.provider_status = &status;
        callback(context, &event);
    }
}

void publishDomainLoss(uint64_t lostDomains,
                       lp3_platform_event_callback callback, void *context) {
    if (lostDomains == 0 || callback == NULL) {
        return;
    }
    static const char error[] = "platform helper domain became unavailable";
    lp3_platform_provider_status_v1 status;
    lp3_platform_event_v1 event;
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.state = LP3_PLATFORM_PROVIDER_DEGRADED;
    status.degraded_domains = lostDomains;
    status.error.data = error;
    status.error.size = sizeof(error) - 1;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_PROVIDER_STATUS;
    event.provider_status = &status;
    callback(context, &event);
}

void stopHost(SailfishInstance *instance, int *dataSocket) {
    lp3_launcher_message_v1 reply;
    int receivedFd = -1;

    if (*dataSocket >= 0) {
        close(*dataSocket);
        *dataSocket = -1;
    }
    publishDisconnected(instance);
    if (instance->control < 0 ||
        !sendLauncherCommand(instance->control, LP3_LAUNCHER_STOP_HOST) ||
        !receiveExpectedLauncherReply(
            instance, instance->control, LP3_LAUNCHER_HOST_STOPPED,
            &reply, &receivedFd, 3000, true) ||
        receivedFd >= 0 || reply.type != LP3_LAUNCHER_HOST_STOPPED ||
        reply.status != 0 || reply.process_id != 0) {
        if (receivedFd >= 0) {
            close(receivedFd);
        }
    }
}

bool startHost(SailfishInstance *instance, int *dataSocket, pid_t *helperPid) {
    lp3_launcher_message_v1 reply;
    int socket = -1;
    const uint8_t *buildId = reinterpret_cast<const uint8_t *>(kBuildId);

    if (instance->control < 0 ||
        !sendLauncherCommand(instance->control, LP3_LAUNCHER_START_HOST) ||
        !receiveExpectedLauncherReply(
            instance, instance->control, LP3_LAUNCHER_HOST_STARTED,
            &reply, &socket, 3000, false) ||
        reply.type != LP3_LAUNCHER_HOST_STARTED || reply.status != 0 ||
        reply.process_id <= 1 || socket < 0 || !validateLauncherSocket(socket) ||
        !lp3wire::sendFrame(socket, lp3wire::Hello, 0, buildId, strlen(kBuildId)) ||
        !waitReadableOrStopping(instance, socket, 3000)) {
        if (socket >= 0) {
            close(socket);
            socket = -1;
        }
        stopHost(instance, &socket);
        return false;
    }
    {
        lp3wire::Frame helloAck;
        if (!lp3wire::receiveFrame(socket, &helloAck) ||
            helloAck.type != lp3wire::HelloAck || helloAck.requestId != 0 ||
            helloAck.payload.size() != strlen(kBuildId) ||
            memcmp(&helloAck.payload[0], kBuildId, strlen(kBuildId)) != 0 ||
            !lp3wire::sendFrame(socket, lp3wire::Ready, 0, NULL, 0) ||
            fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK) != 0) {
            close(socket);
            socket = -1;
            stopHost(instance, &socket);
            return false;
        }
    }
    *dataSocket = socket;
    *helperPid = static_cast<pid_t>(reply.process_id);
    {
        std::lock_guard<std::mutex> lock(instance->mutex);
        instance->socket = socket;
        instance->helperPid = *helperPid;
        instance->forceReset = false;
    }
    return true;
}

void failRequests(SailfishInstance *instance, int32_t status) {
    std::lock_guard<std::mutex> lock(instance->mutex);
    for (std::map<uint64_t, std::shared_ptr<Pending> >::iterator it =
             instance->pending.begin(); it != instance->pending.end(); ++it) {
        it->second->status = status;
        it->second->complete = true;
        it->second->condition.notify_all();
    }
    instance->pending.clear();
    instance->tombstones.clear();
    instance->outgoing.clear();
    instance->forceReset = false;
}

lp3_platform_string abiString(const std::string &value) {
    lp3_platform_string result;
    result.data = value.empty() ? NULL : value.data();
    result.size = static_cast<uint32_t>(value.size());
    return result;
}

void publishCalendarReply(uint64_t requestId, int32_t status,
                          const lp3wire::CalendarReplyData &reply,
                          lp3_platform_event_callback callback, void *context) {
    if (callback == NULL) {
        return;
    }
    std::vector<lp3_platform_calendar_v1> calendars(reply.calendars.size());
    for (size_t index = 0; index < reply.calendars.size(); ++index) {
        const lp3wire::CalendarData &source = reply.calendars[index];
        lp3_platform_calendar_v1 &target = calendars[index];
        memset(&target, 0, sizeof(target));
        target.struct_size = sizeof(target);
        target.flags = source.flags;
        target.color_argb = source.colorArgb;
        target.id = abiString(source.id);
        target.name = abiString(source.name);
        target.owner_name = abiString(source.ownerName);
        target.owner_id = abiString(source.ownerId);
    }

    std::vector<std::vector<lp3_platform_calendar_attendee_v1> > attendees(
        reply.events.size());
    std::vector<std::vector<int32_t> > reminders(reply.events.size());
    std::vector<lp3_platform_calendar_event_v1> events(reply.events.size());
    for (size_t eventIndex = 0; eventIndex < reply.events.size(); ++eventIndex) {
        const lp3wire::CalendarEventData &source = reply.events[eventIndex];
        attendees[eventIndex].resize(source.attendees.size());
        for (size_t attendeeIndex = 0;
             attendeeIndex < source.attendees.size(); ++attendeeIndex) {
            const lp3wire::CalendarAttendeeData &sourceAttendee =
                source.attendees[attendeeIndex];
            lp3_platform_calendar_attendee_v1 &targetAttendee =
                attendees[eventIndex][attendeeIndex];
            memset(&targetAttendee, 0, sizeof(targetAttendee));
            targetAttendee.struct_size = sizeof(targetAttendee);
            targetAttendee.flags = sourceAttendee.flags;
            targetAttendee.role = sourceAttendee.role;
            targetAttendee.status = sourceAttendee.status;
            targetAttendee.name = abiString(sourceAttendee.name);
            targetAttendee.email = abiString(sourceAttendee.email);
        }
        reminders[eventIndex] = source.reminderMinutes;
        lp3_platform_calendar_event_v1 &target = events[eventIndex];
        memset(&target, 0, sizeof(target));
        target.struct_size = sizeof(target);
        target.flags = source.flags;
        target.start_ms = source.startMs;
        target.end_ms = source.endMs;
        target.id = abiString(source.id);
        target.title = abiString(source.title);
        target.location = abiString(source.location);
        target.availability = source.availability;
        target.status = source.status;
        target.calendar_id = abiString(source.calendarId);
        target.base_event_id = abiString(source.baseEventId);
        target.description = abiString(source.description);
        target.attendee_count = static_cast<uint32_t>(
            attendees[eventIndex].size());
        target.attendees = attendees[eventIndex].empty() ? NULL :
            &attendees[eventIndex][0];
        target.reminder_count = static_cast<uint32_t>(
            reminders[eventIndex].size());
        target.reminder_minutes = reminders[eventIndex].empty() ? NULL :
            &reminders[eventIndex][0];
    }

    lp3_platform_calendar_snapshot_v1 snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.kind = reply.kind;
    snapshot.next_offset = reply.nextOffset;
    snapshot.calendar_count = static_cast<uint32_t>(calendars.size());
    snapshot.calendars = calendars.empty() ? NULL : &calendars[0];
    snapshot.event_count = static_cast<uint32_t>(events.size());
    snapshot.events = events.empty() ? NULL : &events[0];

    lp3_platform_event_v1 event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_CALENDAR;
    event.request_id = requestId;
    event.status = status;
    event.calendar_snapshot = status == LP3_PLATFORM_OK ? &snapshot : NULL;
    callback(context, &event);
}

void publishContactReply(uint64_t requestId, int32_t status,
                         const lp3wire::ContactReplyData &reply,
                         lp3_platform_event_callback callback, void *context) {
    if (callback == NULL) return;
    std::vector<lp3_platform_contact_v1> contacts(reply.contacts.size());
    for (size_t index = 0; index < reply.contacts.size(); ++index) {
        const lp3wire::ContactData &source = reply.contacts[index];
        lp3_platform_contact_v1 &target = contacts[index];
        memset(&target, 0, sizeof(target));
        target.struct_size = sizeof(target);
        target.flags = source.flags;
        target.id = abiString(source.id);
        target.display_name = abiString(source.displayName);
        target.phone_number = abiString(source.phoneNumber);
        target.avatar.data = source.avatar.empty() ? NULL : &source.avatar[0];
        target.avatar.size = static_cast<uint32_t>(source.avatar.size());
    }
    lp3_platform_contact_snapshot_v1 snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.kind = reply.kind;
    snapshot.next_offset = reply.nextOffset;
    snapshot.contact_count = static_cast<uint32_t>(contacts.size());
    snapshot.contacts = contacts.empty() ? NULL : &contacts[0];

    lp3_platform_event_v1 event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_CONTACT;
    event.request_id = requestId;
    event.status = status;
    event.contact_snapshot = status == LP3_PLATFORM_OK ? &snapshot : NULL;
    callback(context, &event);
}

bool decodeCompletion(uint16_t operation, const std::vector<uint8_t> &payload,
                      int32_t *status, lp3wire::TimeState *time,
                      lp3wire::LocationData *location,
                      lp3wire::CalendarReplyData *calendar,
                      lp3wire::ContactReplyData *contacts) {
    uint32_t decodedStatus;
    if (status == NULL || time == NULL || location == NULL || calendar == NULL ||
        contacts == NULL) {
        return false;
    }
    if (operation == lp3wire::TimeGet) {
        if (!lp3wire::decodeTimeReply(payload, &decodedStatus, time)) {
            return false;
        }
        memset(location, 0, sizeof(*location));
        calendar->calendars.clear();
        calendar->events.clear();
        contacts->contacts.clear();
    } else if (operation == lp3wire::LocationQuery) {
        if (!lp3wire::decodeLocationReply(payload, &decodedStatus, location)) {
            return false;
        }
        memset(time, 0, sizeof(*time));
        calendar->calendars.clear();
        calendar->events.clear();
        contacts->contacts.clear();
    } else if (operation == lp3wire::CalendarQuery) {
        if (!lp3wire::decodeCalendarReply(payload, &decodedStatus, calendar)) {
            return false;
        }
        memset(time, 0, sizeof(*time));
        memset(location, 0, sizeof(*location));
        contacts->contacts.clear();
    } else if (operation == lp3wire::ContactQuery) {
        if (!lp3wire::decodeContactReply(payload, &decodedStatus, contacts)) {
            return false;
        }
        memset(time, 0, sizeof(*time));
        memset(location, 0, sizeof(*location));
        calendar->calendars.clear();
        calendar->events.clear();
    } else if (operation == lp3wire::NotificationCommand ||
               operation == lp3wire::MessageReply ||
               operation == lp3wire::MessageSend ||
               operation == lp3wire::CallCommand ||
               operation == lp3wire::MediaCommand ||
               operation == lp3wire::PebbleBondRemove) {
        if (!lp3wire::decodeStatusReply(payload, operation, &decodedStatus)) {
            return false;
        }
        memset(time, 0, sizeof(*time));
        memset(location, 0, sizeof(*location));
        calendar->calendars.clear();
        calendar->events.clear();
        contacts->contacts.clear();
    } else {
        return false;
    }
    *status = static_cast<int32_t>(decodedStatus);
    return true;
}

bool dispatchFrame(SailfishInstance *instance, const lp3wire::Frame &frame) {
    if (frame.type == lp3wire::Health) {
        lp3wire::HealthState health;
        lp3_platform_event_callback callback;
        void *context;
        uint64_t lostDomains;
        bool wake = false;
        if (frame.requestId != 0 || !lp3wire::decodeHealth(frame.payload, &health)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            const uint64_t commandDomains =
                lp3wire::DomainNotifications | lp3wire::DomainMessaging |
                lp3wire::DomainCalls | lp3wire::DomainMedia |
                lp3wire::DomainCalendar | lp3wire::DomainContacts |
                lp3wire::DomainLocation;
            lostDomains = instance->readyDomains & ~health.readyDomains &
                commandDomains;
            if ((lostDomains & (lp3wire::DomainLocation |
                                lp3wire::DomainCalendar |
                                lp3wire::DomainContacts)) != 0) {
                for (std::map<uint64_t, std::shared_ptr<Pending> >::iterator
                         pending = instance->pending.begin();
                     pending != instance->pending.end();) {
                    const bool lostLocation =
                        pending->second->operation == lp3wire::LocationQuery &&
                        (lostDomains & lp3wire::DomainLocation) != 0;
                    const bool lostCalendar =
                        pending->second->operation == lp3wire::CalendarQuery &&
                        (lostDomains & lp3wire::DomainCalendar) != 0;
                    const bool lostContacts =
                        pending->second->operation == lp3wire::ContactQuery &&
                        (lostDomains & lp3wire::DomainContacts) != 0;
                    if (!lostLocation && !lostCalendar && !lostContacts) {
                        ++pending;
                        continue;
                    }
                    std::vector<uint8_t> cancelFrame;
                    if (instance->tombstones.size() >= kMaximumOutstanding ||
                        instance->outgoing.size() >= kMaximumOutgoing ||
                        !lp3wire::encodeFrame(lp3wire::Cancel, pending->first,
                                              NULL, 0, &cancelFrame)) {
                        instance->forceReset = true;
                        break;
                    }
                    Tombstone tombstone;
                    tombstone.operation = pending->second->operation;
                    tombstone.expires = std::chrono::steady_clock::now() +
                        kCancellationGrace;
                    instance->tombstones[pending->first] = tombstone;
                    instance->outgoing.push_back(cancelFrame);
                    pending = instance->pending.erase(pending);
                    wake = true;
                }
            }
            instance->readyDomains = health.readyDomains;
            instance->degradedDomains = health.degradedDomains;
            instance->failedDomains = health.failedDomains;
            callback = instance->event;
            context = instance->eventContext;
        }
        /*
         * Health is otherwise level-triggered and polled by the daemon.  Do
         * not let a rapid unavailable -> ready transition erase the edge that
         * retires notification, reply, call, or media command authority.
         */
        publishDomainLoss(lostDomains, callback, context);
        if (wake) {
            wakeWorker(instance);
        }
        return true;
    }

    if (frame.type == lp3wire::Event) {
        lp3_platform_event_v1 event;
        lp3_platform_event_callback callback;
        void *context;

        if (frame.requestId != 0 || frame.payload.size() < 2) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            callback = instance->event;
            context = instance->eventContext;
        }
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        event.status = LP3_PLATFORM_OK;
        const uint16_t eventType = lp3wire::get16(&frame.payload[0]);
        if (eventType == lp3wire::TimeChanged) {
            lp3wire::TimeState time;
            lp3_platform_time_state_v1 abiTime;
            {
                std::lock_guard<std::mutex> lock(instance->mutex);
                if ((instance->readyDomains & lp3wire::DomainTime) == 0) {
                    return false;
                }
            }
            if (!lp3wire::decodeTimeChanged(frame.payload, &time)) {
                return false;
            }
            memset(&abiTime, 0, sizeof(abiTime));
            abiTime.struct_size = sizeof(abiTime);
            abiTime.utc_offset_seconds = time.utcOffsetSeconds;
            abiTime.unix_ms = time.unixMs;
            abiTime.is_24_hour = time.is24Hour;
            event.type = LP3_PLATFORM_EVENT_TIME_CHANGED;
            event.time = &abiTime;
            callback(context, &event);
            return true;
        }
        if (eventType == lp3wire::NotificationPosted ||
            eventType == lp3wire::NotificationClosed) {
            uint16_t decodedEvent;
            lp3wire::NotificationData notification;
            lp3_platform_notification_v1 abiNotification;
            {
                std::lock_guard<std::mutex> lock(instance->mutex);
                if ((instance->readyDomains &
                     lp3wire::DomainNotifications) == 0) {
                    return false;
                }
            }
            if (!lp3wire::decodeNotificationEvent(
                    frame.payload, &decodedEvent, &notification) ||
                decodedEvent != eventType) {
                return false;
            }
            memset(&abiNotification, 0, sizeof(abiNotification));
            abiNotification.struct_size = sizeof(abiNotification);
            abiNotification.flags = notification.flags;
            abiNotification.id = abiString(notification.id);
            abiNotification.application_id = abiString(
                notification.applicationId);
            abiNotification.application_name = abiString(
                notification.applicationName);
            abiNotification.title = abiString(notification.title);
            abiNotification.body = abiString(notification.body);
            abiNotification.timestamp_ms = notification.timestampMs;
            abiNotification.close_reason = notification.closeReason;
            abiNotification.replaces_id = abiString(notification.replacesId);
            abiNotification.category = abiString(notification.category);
            abiNotification.icon_name = abiString(notification.iconName);
            event.type = eventType == lp3wire::NotificationPosted ?
                LP3_PLATFORM_EVENT_NOTIFICATION :
                LP3_PLATFORM_EVENT_NOTIFICATION_CLOSED;
            event.notification = &abiNotification;
            callback(context, &event);
            return true;
        }
        if (eventType == lp3wire::CallChanged) {
            lp3wire::CallData call;
            lp3_platform_call_state_v1 abiCall;
            {
                std::lock_guard<std::mutex> lock(instance->mutex);
                if ((instance->readyDomains & lp3wire::DomainCalls) == 0) {
                    return false;
                }
            }
            if (!lp3wire::decodeCallChanged(frame.payload, &call)) {
                return false;
            }
            memset(&abiCall, 0, sizeof(abiCall));
            abiCall.struct_size = sizeof(abiCall);
            abiCall.state = call.state;
            abiCall.id = abiString(call.id);
            abiCall.name = abiString(call.name);
            abiCall.number = abiString(call.number);
            event.type = LP3_PLATFORM_EVENT_CALL;
            event.call = &abiCall;
            callback(context, &event);
            return true;
        }
        if (eventType == lp3wire::MediaVolumeChanged) {
            lp3wire::MediaVolumeData media;
            lp3_platform_media_state_v1 abiMedia;
            {
                std::lock_guard<std::mutex> lock(instance->mutex);
                if ((instance->readyDomains & lp3wire::DomainMedia) == 0) {
                    return false;
                }
            }
            if (!lp3wire::decodeMediaVolumeChanged(frame.payload, &media)) {
                return false;
            }
            memset(&abiMedia, 0, sizeof(abiMedia));
            abiMedia.struct_size = sizeof(abiMedia);
            abiMedia.flags = media.flags;
            abiMedia.volume_percent = media.volumePercent;
            event.type = LP3_PLATFORM_EVENT_MEDIA;
            event.media = &abiMedia;
            callback(context, &event);
            return true;
        }
        if (eventType == lp3wire::CalendarChanged) {
            {
                std::lock_guard<std::mutex> lock(instance->mutex);
                if ((instance->readyDomains &
                     lp3wire::DomainCalendar) == 0) {
                    return false;
                }
            }
            if (!lp3wire::decodeCalendarChanged(frame.payload)) {
                return false;
            }
            event.type = LP3_PLATFORM_EVENT_CALENDAR;
            callback(context, &event);
            return true;
        }
        if (eventType == lp3wire::ContactChanged) {
            {
                std::lock_guard<std::mutex> lock(instance->mutex);
                if ((instance->readyDomains & lp3wire::DomainContacts) == 0) {
                    return false;
                }
            }
            if (!lp3wire::decodeContactChanged(frame.payload)) return false;
            event.type = LP3_PLATFORM_EVENT_CONTACT;
            callback(context, &event);
            return true;
        }
        return false;
    }

    if (frame.type != lp3wire::Complete && frame.type != lp3wire::Cancelled) {
        return false;
    }
    if (frame.requestId == 0 ||
        (frame.type == lp3wire::Cancelled && !frame.payload.empty())) {
        return false;
    }

    bool publishLocation = false;
    bool publishCalendar = false;
    bool publishContacts = false;
    int32_t locationStatus = LP3_PLATFORM_CANCELLED;
    lp3wire::LocationData location = {};
    lp3wire::CalendarReplyData calendar;
    calendar.kind = lp3wire::CalendarQueryCalendars;
    calendar.nextOffset = 0;
    lp3wire::ContactReplyData contacts;
    contacts.kind = lp3wire::ContactQueryList;
    contacts.nextOffset = 0;
    lp3_platform_event_callback callback = NULL;
    void *context = NULL;
    {
        std::lock_guard<std::mutex> lock(instance->mutex);
        std::map<uint64_t, Tombstone>::iterator tombstone =
            instance->tombstones.find(frame.requestId);
        if (tombstone != instance->tombstones.end()) {
            if (frame.type == lp3wire::Complete) {
                int32_t ignoredStatus;
                lp3wire::TimeState ignoredTime;
                lp3wire::LocationData ignoredLocation;
                lp3wire::CalendarReplyData ignoredCalendar;
                lp3wire::ContactReplyData ignoredContacts;
                if (!decodeCompletion(tombstone->second.operation, frame.payload,
                                      &ignoredStatus, &ignoredTime,
                                      &ignoredLocation, &ignoredCalendar,
                                      &ignoredContacts)) {
                    return false;
                }
            }
            instance->tombstones.erase(tombstone);
            return true;
        }

        std::map<uint64_t, std::shared_ptr<Pending> >::iterator pending =
            instance->pending.find(frame.requestId);
        if (pending == instance->pending.end()) {
            return false;
        }
        if (frame.type == lp3wire::Complete) {
            lp3wire::TimeState time;
            if (!decodeCompletion(pending->second->operation, frame.payload,
                                  &locationStatus, &time, &location,
                                  &calendar, &contacts)) {
                return false;
            }
            pending->second->status = locationStatus;
            pending->second->time = time;
            pending->second->location = location;
            pending->second->calendar = calendar;
            pending->second->contacts = contacts;
        } else {
            pending->second->status = LP3_PLATFORM_CANCELLED;
            locationStatus = LP3_PLATFORM_CANCELLED;
        }
        pending->second->complete = true;
        pending->second->condition.notify_all();
        publishLocation = pending->second->operation == lp3wire::LocationQuery;
        publishCalendar = pending->second->operation == lp3wire::CalendarQuery;
        publishContacts = pending->second->operation == lp3wire::ContactQuery;
        if (publishLocation || publishCalendar || publishContacts) {
            locationStatus = pending->second->status;
            location = pending->second->location;
            calendar = pending->second->calendar;
            contacts = pending->second->contacts;
            callback = instance->event;
            context = instance->eventContext;
        }
        instance->pending.erase(pending);
    }
    if (publishLocation && callback != NULL) {
        lp3_platform_location_v1 abiLocation;
        lp3_platform_event_v1 event;
        memset(&abiLocation, 0, sizeof(abiLocation));
        abiLocation.struct_size = sizeof(abiLocation);
        abiLocation.latitude_e7 = location.latitudeE7;
        abiLocation.longitude_e7 = location.longitudeE7;
        abiLocation.accuracy_m = location.accuracyM;
        abiLocation.timestamp_ms = location.timestampMs;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        event.type = LP3_PLATFORM_EVENT_LOCATION;
        event.request_id = frame.requestId;
        event.status = locationStatus;
        event.location = locationStatus == LP3_PLATFORM_OK ? &abiLocation : NULL;
        callback(context, &event);
    }
    if (publishCalendar) {
        publishCalendarReply(frame.requestId, locationStatus, calendar,
                             callback, context);
    }
    if (publishContacts) {
        publishContactReply(frame.requestId, locationStatus, contacts,
                            callback, context);
    }
    return true;
}

bool flushOutgoing(SailfishInstance *instance, int socket) {
    while (true) {
        std::vector<uint8_t> frame;
        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            if (instance->forceReset) {
                return false;
            }
            if (instance->outgoing.empty()) {
                return true;
            }
            frame = instance->outgoing.front();
        }
        const lp3wire::IoResult result = lp3wire::sendPacket(socket, frame, true);
        if (result == lp3wire::IoWouldBlock) {
            return true;
        }
        if (result != lp3wire::IoFrame) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            if (instance->outgoing.empty() || instance->outgoing.front() != frame) {
                return false;
            }
            instance->outgoing.pop_front();
        }
    }
}

bool runConnection(SailfishInstance *instance, int socket) {
    while (!instance->stopping.load()) {
        short socketEvents = POLLIN | POLLHUP | POLLERR;
        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            if (instance->forceReset) {
                return false;
            }
            const std::chrono::steady_clock::time_point now =
                std::chrono::steady_clock::now();
            for (std::map<uint64_t, Tombstone>::const_iterator
                     it = instance->tombstones.begin();
                 it != instance->tombstones.end(); ++it) {
                if (it->second.expires <= now) {
                    return false;
                }
            }
            if (!instance->outgoing.empty()) {
                socketEvents |= POLLOUT;
            }
        }
        struct pollfd descriptors[2] = {
            { socket, socketEvents, 0 },
            { instance->wake, POLLIN, 0 },
        };
        int result;
        do {
            result = poll(descriptors, 2, 500);
        } while (result < 0 && errno == EINTR && !instance->stopping.load());
        if (instance->stopping.load()) {
            return true;
        }
        if (result < 0 ||
            (descriptors[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            return false;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            drainWake(instance->wake);
        }
        if (!flushOutgoing(instance, socket)) {
            return false;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            while (true) {
                lp3wire::Frame frame;
                const lp3wire::IoResult received =
                    lp3wire::receiveFrameResult(socket, &frame, true);
                if (received == lp3wire::IoWouldBlock) {
                    break;
                }
                if (received != lp3wire::IoFrame || !dispatchFrame(instance, frame)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool waitBeforeRestart(SailfishInstance *instance, unsigned int delaySeconds) {
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now() + std::chrono::seconds(delaySeconds);
    while (!instance->stopping.load() && std::chrono::steady_clock::now() < end) {
        struct pollfd descriptor = { instance->wake, POLLIN, 0 };
        int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            end - std::chrono::steady_clock::now()).count());
        int result;
        do {
            result = poll(&descriptor, 1, remaining > 0 ? remaining : 0);
        } while (result < 0 && errno == EINTR && !instance->stopping.load());
        if (result > 0 && (descriptor.revents & POLLIN) != 0) {
            drainWake(instance->wake);
        }
    }
    return !instance->stopping.load();
}

bool recordFailure(SailfishInstance *instance, unsigned int *failures,
                   time_t *firstFailure) {
    const time_t now = time(NULL);
    if (*firstFailure == 0 || now - *firstFailure > kFailureWindowSeconds) {
        *firstFailure = now;
        *failures = 0;
    }
    if (*failures >= kMaximumFailures) {
        std::lock_guard<std::mutex> lock(instance->mutex);
        instance->latched = true;
        return false;
    }
    const unsigned int delay = kRestartDelays[*failures];
    ++*failures;
    return waitBeforeRestart(instance, delay);
}

void hostWorker(SailfishInstance *instance) {
    unsigned int failures = 0;
    time_t firstFailure = 0;
    int socket = -1;
    pid_t helperPid = -1;

    while (!instance->stopping.load()) {
        if (!startHost(instance, &socket, &helperPid)) {
            failRequests(instance, LP3_PLATFORM_UNAVAILABLE);
            if (!recordFailure(instance, &failures, &firstFailure)) {
                break;
            }
            continue;
        }
        const bool cleanStop = runConnection(instance, socket);
        stopHost(instance, &socket);
        failRequests(instance, LP3_PLATFORM_UNAVAILABLE);
        if (cleanStop || instance->stopping.load()) {
            break;
        }
        if (!recordFailure(instance, &failures, &firstFailure)) {
            break;
        }
    }
    if (socket >= 0) {
        stopHost(instance, &socket);
    }
    publishDisconnected(instance);
}

int32_t probe(const lp3_platform_host_v1 *host) {
    if (host == NULL || host->struct_size < sizeof(lp3_platform_host_v1) ||
        host->abi_major != LP3_PLATFORM_ABI_MAJOR || host->event == NULL ||
        host->max_queued_events == 0 || host->max_payload_bytes == 0) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    return LP3_PLATFORM_OK;
}

int32_t create(const lp3_platform_host_v1 *host, lp3_platform_instance **out) {
    if (probe(host) != LP3_PLATFORM_OK || out == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    SailfishInstance *instance = new SailfishInstance;
    instance->event = host->event;
    instance->eventContext = host->event_context;
    instance->control = fcntl(LP3_LAUNCHER_CONTROL_FD, F_DUPFD_CLOEXEC, 4);
    if (instance->control < 0 || !validateLauncherSocket(instance->control)) {
        if (instance->control >= 0) {
            close(instance->control);
            instance->control = -1;
        }
    }
    instance->wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (instance->wake < 0) {
        if (instance->control >= 0) {
            close(instance->control);
        }
        delete instance;
        return LP3_PLATFORM_IO_ERROR;
    }
    *out = instance;
    return LP3_PLATFORM_OK;
}

int32_t start(lp3_platform_instance *raw) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    if (instance == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    {
        std::lock_guard<std::mutex> lock(instance->mutex);
        if (instance->started) {
            return LP3_PLATFORM_OK;
        }
        instance->started = true;
    }
    if (instance->control >= 0) {
        instance->worker = std::thread(hostWorker, instance);
    }
    return LP3_PLATFORM_OK;
}

int32_t cancel(lp3_platform_instance *raw, uint64_t requestId) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    Tombstone tombstone;
    std::vector<uint8_t> frame;

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(instance->mutex);
    std::map<uint64_t, std::shared_ptr<Pending> >::iterator pending =
        instance->pending.find(requestId);
    if (pending == instance->pending.end() ||
        (pending->second->operation != lp3wire::LocationQuery &&
         pending->second->operation != lp3wire::CalendarQuery &&
         pending->second->operation != lp3wire::ContactQuery)) {
        return LP3_PLATFORM_NOT_SUPPORTED;
    }
    if (instance->stopping.load() || instance->socket < 0 || instance->latched) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->tombstones.size() >= kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        !lp3wire::encodeFrame(lp3wire::Cancel, requestId, NULL, 0, &frame)) {
        instance->forceReset = true;
        wakeWorker(instance);
        return LP3_PLATFORM_UNAVAILABLE;
    }
    tombstone.operation = pending->second->operation;
    tombstone.expires = std::chrono::steady_clock::now() + kCancellationGrace;
    instance->pending.erase(pending);
    instance->tombstones[requestId] = tombstone;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);
    return LP3_PLATFORM_OK;
}

int32_t requestStop(lp3_platform_instance *raw) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    if (instance == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    instance->stopping.store(true);
    wakeWorker(instance);
    if (instance->worker.joinable()) {
        instance->worker.join();
    }
    failRequests(instance, LP3_PLATFORM_UNAVAILABLE);
    if (instance->control >= 0) {
        close(instance->control);
        instance->control = -1;
    }
    if (instance->wake >= 0) {
        close(instance->wake);
        instance->wake = -1;
    }
    return LP3_PLATFORM_OK;
}

void destroy(lp3_platform_instance *raw) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    if (instance == NULL) {
        return;
    }
    (void)requestStop(raw);
    delete instance;
}

int32_t notSupportedNotification(lp3_platform_instance *, uint64_t,
                                 const lp3_platform_notification_v1 *) {
    return LP3_PLATFORM_NOT_SUPPORTED;
}

int32_t calendarQuery(lp3_platform_instance *raw, uint64_t requestId,
                      const lp3_platform_calendar_query_v1 *request) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::CalendarQueryData query = {};
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::CalendarQuery));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || request == NULL ||
        request->struct_size < sizeof(*request) ||
        request->calendar_id.size > LP3_PLATFORM_CALENDAR_ID_MAX ||
        (request->calendar_id.size != 0 && request->calendar_id.data == NULL)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    query.kind = request->kind;
    query.maxRecords = request->max_records;
    query.offset = request->offset;
    query.startMs = request->start_ms;
    query.endMs = request->end_ms;
    if (request->calendar_id.size != 0) {
        query.calendarId.assign(request->calendar_id.data,
                                request->calendar_id.size);
    }
    pending->calendar.kind = query.kind;
    if (!lp3wire::encodeCalendarQuery(query, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 || instance->latched ||
        (instance->readyDomains & lp3wire::DomainCalendar) == 0) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);
    return LP3_PLATFORM_OK;
}

int32_t contactQuery(lp3_platform_instance *raw, uint64_t requestId,
                     const lp3_platform_contact_query_v1 *request) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::ContactQueryData query = {};
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::ContactQuery));
    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || request == NULL ||
        request->struct_size < sizeof(*request) ||
        request->query.size > LP3_PLATFORM_CONTACT_NUMBER_MAX ||
        (request->query.size != 0 && request->query.data == NULL)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    query.kind = request->kind;
    query.maxRecords = request->max_records;
    query.offset = request->offset;
    if (request->query.size != 0) {
        query.query.assign(request->query.data, request->query.size);
    }
    pending->contacts.kind = query.kind;
    if (!lp3wire::encodeContactQuery(query, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 || instance->latched ||
        (instance->readyDomains & lp3wire::DomainContacts) == 0) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);
    return LP3_PLATFORM_OK;
}

int32_t locationQuery(lp3_platform_instance *raw, uint64_t requestId,
                      const lp3_platform_location_request_v1 *request) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::LocationQueryData query;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::LocationQuery));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || request == NULL ||
        request->struct_size < sizeof(*request)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    query.accuracy = request->accuracy;
    query.timeoutMs = request->timeout_ms;
    if (!lp3wire::encodeLocationQuery(query, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 || instance->latched ||
        (instance->readyDomains & lp3wire::DomainLocation) == 0) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);
    return LP3_PLATFORM_OK;
}

int32_t notSupportedProfile(lp3_platform_instance *, uint64_t,
                            const lp3_platform_profile_request_v1 *) {
    return LP3_PLATFORM_NOT_SUPPORTED;
}

int32_t notificationCommand(
    lp3_platform_instance *raw, uint64_t requestId,
    const lp3_platform_notification_command_v1 *command) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::NotificationCommandData wireCommand;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    uint64_t requiredDomains;
    std::shared_ptr<Pending> pending(
        new Pending(lp3wire::NotificationCommand));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || command == NULL ||
        command->struct_size < sizeof(*command) || command->id.size == 0 ||
        command->id.size > LP3_PLATFORM_NOTIFICATION_ID_MAX ||
        command->id.data == NULL ||
        (command->command != LP3_PLATFORM_NOTIFICATION_DISMISS &&
         command->command != LP3_PLATFORM_NOTIFICATION_OPEN)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    wireCommand.command = command->command;
    wireCommand.id.assign(command->id.data, command->id.size);
    requiredDomains = lp3wire::DomainNotifications;
    if (wireCommand.command == lp3wire::NotificationOpen) {
        requiredDomains |= lp3wire::DomainMessaging;
    }
    if (!lp3wire::encodeNotificationCommand(wireCommand, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 ||
        instance->latched ||
        (instance->readyDomains & requiredDomains) != requiredDomains) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kRequestTimeout,
                                     [pending, instance] {
                                         return pending->complete ||
                                             instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                Tombstone tombstone;
                std::vector<uint8_t> cancelFrame;
                tombstone.operation = lp3wire::NotificationCommand;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId,
                                          NULL, 0, &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    return pending->complete ? pending->status : LP3_PLATFORM_UNAVAILABLE;
}

int32_t replyMessage(lp3_platform_instance *raw, uint64_t requestId,
                     const lp3_platform_message_v1 *message) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::MessageReplyData wireReply;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::MessageReply));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || message == NULL ||
        message->struct_size < sizeof(*message) || message->flags != 0 ||
        message->conversation_id.size == 0 ||
        message->conversation_id.size >
            LP3_PLATFORM_MESSAGE_CONVERSATION_ID_MAX ||
        message->conversation_id.data == NULL || message->recipient.size != 0 ||
        message->text.size == 0 ||
        message->text.size > LP3_PLATFORM_MESSAGE_TEXT_MAX ||
        message->text.data == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    wireReply.notificationId.assign(message->conversation_id.data,
                                    message->conversation_id.size);
    wireReply.text.assign(message->text.data, message->text.size);
    if (!lp3wire::encodeMessageReply(wireReply, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(instance->mutex);
    const uint64_t requiredDomains = lp3wire::DomainNotifications |
        lp3wire::DomainMessaging;
    if (instance->stopping.load() || instance->socket < 0 ||
        instance->latched ||
        (instance->readyDomains & requiredDomains) != requiredDomains) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kRequestTimeout,
                                     [pending, instance] {
                                         return pending->complete ||
                                             instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                Tombstone tombstone;
                std::vector<uint8_t> cancelFrame;
                tombstone.operation = lp3wire::MessageReply;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId,
                                          NULL, 0, &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    return pending->complete ? pending->status : LP3_PLATFORM_UNAVAILABLE;
}

int32_t sendMessage(lp3_platform_instance *raw, uint64_t requestId,
                    const lp3_platform_outgoing_message_v1 *message) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::MessageSendData wireMessage;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::MessageSend));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || message == NULL ||
        message->struct_size < sizeof(*message) || message->flags != 0 ||
        message->account_id.size == 0 ||
        message->account_id.size > LP3_PLATFORM_MESSAGE_ACCOUNT_ID_MAX ||
        message->account_id.data == NULL || message->recipient.size == 0 ||
        message->recipient.size > LP3_PLATFORM_MESSAGE_RECIPIENT_MAX ||
        message->recipient.data == NULL || message->text.size == 0 ||
        message->text.size > LP3_PLATFORM_MESSAGE_TEXT_MAX ||
        message->text.data == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    wireMessage.accountId.assign(message->account_id.data,
                                 message->account_id.size);
    wireMessage.recipient.assign(message->recipient.data,
                                 message->recipient.size);
    wireMessage.text.assign(message->text.data, message->text.size);
    if (!lp3wire::encodeMessageSend(wireMessage, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 ||
        instance->latched ||
        (instance->readyDomains & lp3wire::DomainMessaging) == 0) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kRequestTimeout,
                                     [pending, instance] {
                                         return pending->complete ||
                                             instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                Tombstone tombstone;
                std::vector<uint8_t> cancelFrame;
                tombstone.operation = lp3wire::MessageSend;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId,
                                          NULL, 0, &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    return pending->complete ? pending->status : LP3_PLATFORM_UNAVAILABLE;
}

int32_t callCommand(lp3_platform_instance *raw, uint64_t requestId,
                    const lp3_platform_call_command_v1 *command) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::CallCommandData wireCommand;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::CallCommand));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || command == NULL ||
        command->struct_size < sizeof(*command) ||
        (command->command != LP3_PLATFORM_CALL_ANSWER &&
         command->command != LP3_PLATFORM_CALL_HANG_UP &&
         command->command != LP3_PLATFORM_CALL_SILENCE)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    if (command->command == LP3_PLATFORM_CALL_SILENCE) {
        if (command->call_id.size != 0) {
            return LP3_PLATFORM_INVALID_ARGUMENT;
        }
    } else if (command->call_id.size == 0 ||
               command->call_id.size > LP3_PLATFORM_CALL_ID_MAX ||
               command->call_id.data == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    wireCommand.command = command->command;
    if (command->call_id.size != 0) {
        wireCommand.id.assign(command->call_id.data, command->call_id.size);
    }
    if (!lp3wire::encodeCallCommand(wireCommand, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 ||
        instance->latched ||
        (instance->readyDomains & lp3wire::DomainCalls) == 0) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kRequestTimeout,
                                     [pending, instance] {
                                         return pending->complete ||
                                             instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                Tombstone tombstone;
                std::vector<uint8_t> cancelFrame;
                tombstone.operation = lp3wire::CallCommand;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId,
                                          NULL, 0, &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    return pending->complete ? pending->status : LP3_PLATFORM_UNAVAILABLE;
}

int32_t mediaCommand(lp3_platform_instance *raw, uint64_t requestId,
                     uint32_t command) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::MediaCommandData wireCommand;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::MediaCommand));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 ||
        (command != LP3_PLATFORM_MEDIA_VOLUME_UP &&
         command != LP3_PLATFORM_MEDIA_VOLUME_DOWN)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    wireCommand.command = command;
    if (!lp3wire::encodeMediaCommand(wireCommand, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 ||
        instance->latched ||
        (instance->readyDomains & lp3wire::DomainMedia) == 0) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kRequestTimeout,
                                     [pending, instance] {
                                         return pending->complete ||
                                             instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                Tombstone tombstone;
                std::vector<uint8_t> cancelFrame;
                tombstone.operation = lp3wire::MediaCommand;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId,
                                          NULL, 0, &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    return pending->complete ? pending->status : LP3_PLATFORM_UNAVAILABLE;
}

int32_t removePebbleBond(lp3_platform_instance *raw, uint64_t requestId,
                         const lp3_platform_pebble_bond_v1 *bond) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    lp3wire::PebbleBondRemoveData request = {};
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::PebbleBondRemove));

    if (instance == NULL || requestId == 0 ||
        (requestId & (UINT64_C(1) << 63)) != 0 || bond == NULL ||
        bond->struct_size < sizeof(*bond) || bond->reserved[0] != 0 ||
        bond->reserved[1] != 0) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    request.adapterIndex = bond->adapter_index;
    memcpy(request.address, bond->address, sizeof(request.address));
    if (!lp3wire::encodePebbleBondRemove(request, &payload) ||
        !lp3wire::encodeFrame(lp3wire::Request, requestId,
                              &payload[0], payload.size(), &frame)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 ||
        instance->latched) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >=
            kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing ||
        instance->pending.count(requestId) != 0 ||
        instance->tombstones.count(requestId) != 0) {
        return LP3_PLATFORM_BUSY;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kBondRemovalTimeout,
                                     [pending, instance] {
                                         return pending->complete ||
                                             instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                Tombstone tombstone;
                std::vector<uint8_t> cancelFrame;
                tombstone.operation = lp3wire::PebbleBondRemove;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId,
                                          NULL, 0, &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    return pending->complete ? pending->status : LP3_PLATFORM_UNAVAILABLE;
}

int32_t getTimeState(lp3_platform_instance *raw, lp3_platform_time_state_v1 *out) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame;
    std::shared_ptr<Pending> pending(new Pending(lp3wire::TimeGet));
    uint64_t requestId;

    if (instance == NULL || out == NULL || out->struct_size < sizeof(*out)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    if (!lp3wire::encodeTimeGet(&payload)) {
        return LP3_PLATFORM_INTERNAL_ERROR;
    }
    std::unique_lock<std::mutex> lock(instance->mutex);
    if (instance->stopping.load() || instance->socket < 0 || instance->latched) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (instance->pending.size() + instance->tombstones.size() >= kMaximumOutstanding ||
        instance->outgoing.size() >= kMaximumOutgoing || instance->nextRequestId == 0) {
        return LP3_PLATFORM_BUSY;
    }
    requestId = instance->nextRequestId++;
    if (!lp3wire::encodeFrame(lp3wire::Request, requestId, &payload[0], payload.size(),
                              &frame)) {
        return LP3_PLATFORM_INTERNAL_ERROR;
    }
    instance->pending[requestId] = pending;
    instance->outgoing.push_back(frame);
    wakeWorker(instance);

    if (!pending->condition.wait_for(lock, kRequestTimeout,
                                     [pending, instance] {
                                         return pending->complete || instance->stopping.load();
                                     })) {
        std::map<uint64_t, std::shared_ptr<Pending> >::iterator current =
            instance->pending.find(requestId);
        if (current != instance->pending.end()) {
            instance->pending.erase(current);
            if (instance->tombstones.size() >= kMaximumOutstanding ||
                instance->outgoing.size() >= kMaximumOutgoing) {
                instance->forceReset = true;
            } else {
                std::vector<uint8_t> cancelFrame;
                Tombstone tombstone;
                tombstone.operation = lp3wire::TimeGet;
                tombstone.expires =
                    std::chrono::steady_clock::now() + kCancellationGrace;
                instance->tombstones[requestId] = tombstone;
                if (!lp3wire::encodeFrame(lp3wire::Cancel, requestId, NULL, 0,
                                          &cancelFrame)) {
                    instance->forceReset = true;
                } else {
                    instance->outgoing.push_back(cancelFrame);
                }
            }
            wakeWorker(instance);
        }
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (!pending->complete) {
        return LP3_PLATFORM_UNAVAILABLE;
    }
    if (pending->status != LP3_PLATFORM_OK) {
        return pending->status;
    }
    const lp3wire::TimeState time = pending->time;
    lock.unlock();
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    out->utc_offset_seconds = time.utcOffsetSeconds;
    out->unix_ms = time.unixMs;
    out->is_24_hour = time.is24Hour;
    return LP3_PLATFORM_OK;
}

int32_t notSupportedDeviceState(lp3_platform_instance *, lp3_platform_device_state_v1 *) {
    return LP3_PLATFORM_NOT_SUPPORTED;
}

int32_t getStatus(lp3_platform_instance *raw, lp3_platform_provider_status_v1 *out) {
    SailfishInstance *instance = static_cast<SailfishInstance *>(raw);
    const char *error;
    const uint64_t supported = kSupportedDomains;

    if (instance == NULL || out == NULL || out->struct_size < sizeof(*out)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(instance->mutex);
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    out->helper_pid = instance->helperPid > 0 ?
        static_cast<int64_t>(instance->helperPid) : 0;
    if (instance->latched) {
        out->state = LP3_PLATFORM_PROVIDER_FAILED;
        out->failed_domains = supported;
        error = "helper restart failure latch";
    } else if (instance->helperPid > 0 && instance->socket >= 0) {
        out->ready_domains = instance->readyDomains;
        out->degraded_domains = instance->degradedDomains;
        out->failed_domains = instance->failedDomains;
        if ((out->ready_domains | out->degraded_domains |
             out->failed_domains) != supported) {
            out->state = LP3_PLATFORM_PROVIDER_DEGRADED;
            // A positive PID tells the daemon that restart reached a usable,
            // fully-classified helper.  Do not expose it before first Health.
            out->helper_pid = 0;
            out->degraded_domains = supported;
            out->ready_domains = 0;
            out->failed_domains = 0;
            error = "helper health is pending";
        } else if (out->failed_domains == supported) {
            out->state = LP3_PLATFORM_PROVIDER_FAILED;
            error = "helper domains failed";
        } else if (out->degraded_domains != 0 || out->failed_domains != 0) {
            out->state = LP3_PLATFORM_PROVIDER_DEGRADED;
            error = "some helper domains are unavailable";
        } else {
            out->state = LP3_PLATFORM_PROVIDER_READY;
            error = "";
        }
    } else if (instance->control < 0) {
        out->state = LP3_PLATFORM_PROVIDER_DEGRADED;
        out->degraded_domains = supported;
        error = "session launcher is unavailable";
    } else {
        out->state = LP3_PLATFORM_PROVIDER_DEGRADED;
        out->degraded_domains = supported;
        error = "helper is restarting";
    }
    out->error.data = error;
    out->error.size = static_cast<uint32_t>(strlen(error));
    return LP3_PLATFORM_OK;
}

const lp3_platform_api_v1 kApi = {
    sizeof(lp3_platform_api_v1),
    {
        sizeof(lp3_platform_provider_info_v1),
        LP3_PLATFORM_ABI_MAJOR,
        LP3_PLATFORM_ABI_MINOR,
        LP3_PLATFORM_DOMAIN_TIME | LP3_PLATFORM_DOMAIN_NOTIFICATIONS |
            LP3_PLATFORM_DOMAIN_MESSAGING |
            LP3_PLATFORM_DOMAIN_MEDIA | LP3_PLATFORM_DOMAIN_CALLS |
            LP3_PLATFORM_DOMAIN_CALENDAR | LP3_PLATFORM_DOMAIN_CONTACTS |
            LP3_PLATFORM_DOMAIN_LOCATION,
        { kProviderName, sizeof(kProviderName) - 1 },
        { kBuildId, sizeof(kBuildId) - 1 },
    },
    probe,
    create,
    start,
    cancel,
    requestStop,
    destroy,
    notSupportedNotification,
    replyMessage,
    mediaCommand,
    callCommand,
    calendarQuery,
    contactQuery,
    locationQuery,
    notSupportedProfile,
    getTimeState,
    notSupportedDeviceState,
    getStatus,
    notificationCommand,
    sendMessage,
    removePebbleBond,
};

} // namespace

extern "C" __attribute__((visibility("default"))) int32_t
lp3_platform_get_api(uint32_t hostAbiMajor, uint32_t hostAbiMinor,
                     const lp3_platform_api_v1 **api) {
    if (api == NULL || hostAbiMajor != LP3_PLATFORM_ABI_MAJOR ||
        hostAbiMinor < LP3_PLATFORM_ABI_MINOR) {
        return LP3_PLATFORM_NOT_SUPPORTED;
    }
    *api = &kApi;
    return LP3_PLATFORM_OK;
}
