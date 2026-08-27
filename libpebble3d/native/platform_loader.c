/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native Image JNI bridge for the libpebble3d platform-provider ABI.
 *
 * The public daemon never accepts a provider path.  This bridge discovers one
 * provider below the package-owned directory, validates it using directory
 * descriptors, then owns its full lifecycle.  It intentionally has no Qt,
 * D-Bus, JSON, or application-specific command handling.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "libpebble3d-launcher-wire.h"
#include "libpebble3d-platform.h"

#ifndef LP3_PLATFORM_DIRECTORY
#define LP3_PLATFORM_DIRECTORY "/usr/lib64/libpebble3d/platforms"
#endif
#define LP3_PROVIDER_PREFIX "libpebble3d-platform-"
#define LP3_PROVIDER_SUFFIX ".so"
#define LP3_MAX_FIELD 192
#define LP3_MAX_PATH 512
#define LP3_MAX_QUEUED_EVENTS 64u
#define LP3_MAX_PAYLOAD_BYTES (64u * 1024u)
#define LP3_LOCATION_TIMEOUT_MAX_MS (30u * 1000u)

typedef int32_t (*lp3_get_api_fn)(uint32_t, uint32_t,
                                  const struct lp3_platform_api_v1 **);

struct lp3_loader_snapshot {
    char state[16];
    char provider[LP3_MAX_FIELD];
    char build_id[LP3_MAX_FIELD];
    char abi_version[16];
    char domains[32];
    char helper_pid[32];
    char error[LP3_MAX_FIELD];
    char supported_domains[32];
    char degraded_domains[32];
    char failed_domains[32];
};

struct lp3_loader {
    void *library;
    const struct lp3_platform_api_v1 *api;
    struct lp3_platform_instance *instance;
    struct lp3_loader_snapshot snapshot;
    uint64_t next_request_id;
};

struct lp3_copied_event {
    uint32_t type;
    int32_t utc_offset_seconds;
    int64_t unix_ms;
    uint32_t is_24_hour;
};

struct lp3_copied_notification_event {
    uint32_t type;
    uint32_t flags;
    int64_t timestamp_ms;
    uint32_t close_reason;
    char id[LP3_PLATFORM_NOTIFICATION_ID_MAX + 1];
    char replaces_id[LP3_PLATFORM_NOTIFICATION_ID_MAX + 1];
    char application_id[LP3_PLATFORM_NOTIFICATION_APPLICATION_ID_MAX + 1];
    char application_name[LP3_PLATFORM_NOTIFICATION_APPLICATION_NAME_MAX + 1];
    char title[LP3_PLATFORM_NOTIFICATION_TITLE_MAX + 1];
    char body[LP3_PLATFORM_NOTIFICATION_BODY_MAX + 1];
    char category[LP3_PLATFORM_NOTIFICATION_CATEGORY_MAX + 1];
    char icon_name[LP3_PLATFORM_NOTIFICATION_ICON_NAME_MAX + 1];
};

struct lp3_copied_call_event {
    uint32_t state;
    char id[LP3_PLATFORM_CALL_ID_MAX + 1];
    char name[LP3_PLATFORM_CALL_NAME_MAX + 1];
    char number[LP3_PLATFORM_CALL_NUMBER_MAX + 1];
};

struct lp3_copied_media_event {
    uint32_t flags;
    int32_t volume_percent;
};

struct lp3_copied_location_event {
    uint64_t request_id;
    int32_t status;
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t accuracy_m;
    int64_t timestamp_ms;
};

struct lp3_copied_calendar_event {
    uint8_t *payload;
    uint32_t size;
};

struct lp3_copied_contact_event {
    uint8_t *payload;
    uint32_t size;
};

static pthread_mutex_t loader_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int event_batch_held;
static struct lp3_loader loader;
static struct lp3_copied_event event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t event_head;
static size_t event_count;
static struct lp3_copied_notification_event
    notification_event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t notification_event_head;
static size_t notification_event_count;
static struct lp3_copied_call_event call_event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t call_event_head;
static size_t call_event_count;
static struct lp3_copied_media_event media_event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t media_event_head;
static size_t media_event_count;
static struct lp3_copied_location_event
    location_event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t location_event_head;
static size_t location_event_count;
/* One bounded record per admitted request; zero is never a valid request ID. */
static uint64_t location_outstanding[LP3_MAX_QUEUED_EVENTS];
static size_t location_outstanding_count;
/* Cancellation tombstones discard one late completion without retaining it. */
static uint64_t location_tombstones[LP3_MAX_QUEUED_EVENTS];
static size_t location_tombstone_count;
static struct lp3_copied_calendar_event
    calendar_event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t calendar_event_head;
static size_t calendar_event_count;
static uint64_t calendar_outstanding[LP3_MAX_QUEUED_EVENTS];
static size_t calendar_outstanding_count;
static uint64_t calendar_tombstones[LP3_MAX_QUEUED_EVENTS];
static size_t calendar_tombstone_count;
static struct lp3_copied_contact_event
    contact_event_queue[LP3_MAX_QUEUED_EVENTS];
static size_t contact_event_head;
static size_t contact_event_count;
static uint64_t contact_outstanding[LP3_MAX_QUEUED_EVENTS];
static size_t contact_outstanding_count;
static uint64_t contact_tombstones[LP3_MAX_QUEUED_EVENTS];
static size_t contact_tombstone_count;
static uint64_t event_failed_domains;
static int provider_reset_pending;
static size_t provider_command_dispatches;
static pthread_cond_t provider_command_condition = PTHREAD_COND_INITIALIZER;

static void lock_event_queues(void) {
    if (!event_batch_held) {
        pthread_mutex_lock(&event_lock);
    }
}

static void unlock_event_queues(void) {
    if (!event_batch_held) {
        pthread_mutex_unlock(&event_lock);
    }
}

/*
 * Provider commands reserve one dispatch before invoking the provider.  The
 * caller already holds loader_lock, preserving loader_lock -> event_lock.
 * Provider-status callbacks wait for every reservation to retire before they
 * publish a reset, while ordinary command-triggered events remain deliverable.
 */
static int begin_provider_command_dispatch(uint64_t command_domain) {
    lock_event_queues();
    if (provider_reset_pending ||
        (event_failed_domains & command_domain) != 0) {
        unlock_event_queues();
        return 0;
    }
    ++provider_command_dispatches;
    unlock_event_queues();
    return 1;
}

static void end_provider_command_dispatch(void) {
    lock_event_queues();
    if (provider_command_dispatches == 0) {
        unlock_event_queues();
        return;
    }
    --provider_command_dispatches;
    if (provider_command_dispatches == 0) {
        pthread_cond_broadcast(&provider_command_condition);
    }
    unlock_event_queues();
}

static int location_id_index(const uint64_t *ids, size_t count,
                             uint64_t request_id) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (ids[index] == request_id) {
            return (int)index;
        }
    }
    return -1;
}

static void location_remove_id(uint64_t *ids, size_t *count, size_t index) {
    if (index + 1 < *count) {
        memmove(&ids[index], &ids[index + 1],
                (*count - index - 1) * sizeof(ids[0]));
    }
    --*count;
}

static void clear_location_events(void) {
    memset(location_event_queue, 0, sizeof(location_event_queue));
    location_event_head = 0;
    location_event_count = 0;
    memset(location_outstanding, 0, sizeof(location_outstanding));
    location_outstanding_count = 0;
    memset(location_tombstones, 0, sizeof(location_tombstones));
    location_tombstone_count = 0;
}

static void clear_calendar_events(void) {
    size_t index;
    for (index = 0; index < calendar_event_count; ++index) {
        size_t slot = (calendar_event_head + index) % LP3_MAX_QUEUED_EVENTS;
        free(calendar_event_queue[slot].payload);
    }
    memset(calendar_event_queue, 0, sizeof(calendar_event_queue));
    calendar_event_head = 0;
    calendar_event_count = 0;
    memset(calendar_outstanding, 0, sizeof(calendar_outstanding));
    calendar_outstanding_count = 0;
    memset(calendar_tombstones, 0, sizeof(calendar_tombstones));
    calendar_tombstone_count = 0;
}

static void clear_contact_events(void) {
    size_t index;
    for (index = 0; index < contact_event_count; ++index) {
        size_t slot = (contact_event_head + index) % LP3_MAX_QUEUED_EVENTS;
        free(contact_event_queue[slot].payload);
    }
    memset(contact_event_queue, 0, sizeof(contact_event_queue));
    contact_event_head = 0;
    contact_event_count = 0;
    memset(contact_outstanding, 0, sizeof(contact_outstanding));
    contact_outstanding_count = 0;
    memset(contact_tombstones, 0, sizeof(contact_tombstones));
    contact_tombstone_count = 0;
}

/*
 * The session launcher initially gives the daemon only this private control
 * socket, not a privileged host connection.  Mark the process non-dumpable
 * before proving readiness so another same-UID session process cannot inspect
 * the later provider descriptor through /proc.
 */
static int harden_for_session_launcher(void) {
    struct rlimit core_limit;
    struct group *privileged;
    struct ucred credentials;
    socklen_t credentials_length = sizeof(credentials);
    int type = 0;
    socklen_t type_length = sizeof(type);
    gid_t real_group;
    gid_t effective_group;
    gid_t saved_group;
    struct lp3_launcher_message_v1 message;
    ssize_t written;

    core_limit.rlim_cur = 0;
    core_limit.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return 0;
    }
    privileged = getgrnam("privileged");
    if (privileged == NULL ||
        getresgid(&real_group, &effective_group, &saved_group) != 0 ||
        real_group != effective_group || real_group != saved_group ||
        real_group == privileged->gr_gid ||
        getsockopt(LP3_LAUNCHER_CONTROL_FD, SOL_SOCKET, SO_TYPE,
                   &type, &type_length) != 0 || type != SOCK_SEQPACKET ||
        getsockopt(LP3_LAUNCHER_CONTROL_FD, SOL_SOCKET, SO_PEERCRED,
                   &credentials, &credentials_length) != 0 ||
        credentials.pid != getppid() || credentials.uid != getuid() ||
        credentials.gid != privileged->gr_gid ||
        fcntl(LP3_LAUNCHER_CONTROL_FD, F_SETFD, FD_CLOEXEC) != 0) {
        return 0;
    }
    memset(&message, 0, sizeof(message));
    message.magic = LP3_LAUNCHER_MAGIC;
    message.version = LP3_LAUNCHER_VERSION;
    message.type = LP3_LAUNCHER_DAEMON_READY;
    do {
        written = send(LP3_LAUNCHER_CONTROL_FD, &message, sizeof(message),
                       MSG_NOSIGNAL);
    } while (written < 0 && errno == EINTR);
    return written == (ssize_t)sizeof(message);
}

static void set_ascii_field(char *destination, size_t destination_size,
                            const char *source, size_t source_size) {
    size_t index;

    if (destination_size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    if (source_size >= destination_size) {
        source_size = destination_size - 1;
    }
    for (index = 0; index < source_size; ++index) {
        unsigned char value = (unsigned char)source[index];
        destination[index] = (value >= 0x20 && value <= 0x7e) ? (char)value : '?';
    }
    destination[source_size] = '\0';
}

static void set_literal(char *destination, size_t destination_size,
                        const char *source) {
    set_ascii_field(destination, destination_size, source, strlen(source));
}

static void set_snapshot_state(const char *state, const char *error) {
    memset(&loader.snapshot, 0, sizeof(loader.snapshot));
    set_literal(loader.snapshot.state, sizeof(loader.snapshot.state), state);
    set_literal(loader.snapshot.abi_version, sizeof(loader.snapshot.abi_version), "1.7");
    set_literal(loader.snapshot.domains, sizeof(loader.snapshot.domains), "0");
    set_literal(loader.snapshot.helper_pid, sizeof(loader.snapshot.helper_pid), "0");
    set_literal(loader.snapshot.supported_domains,
                sizeof(loader.snapshot.supported_domains), "0");
    set_literal(loader.snapshot.degraded_domains,
                sizeof(loader.snapshot.degraded_domains), "0");
    set_literal(loader.snapshot.failed_domains,
                sizeof(loader.snapshot.failed_domains), "0");
    if (error != NULL) {
        set_literal(loader.snapshot.error, sizeof(loader.snapshot.error), error);
    }
}

static int candidate_name(const char *name) {
    size_t length = strlen(name);
    const size_t prefix_length = strlen(LP3_PROVIDER_PREFIX);
    const size_t suffix_length = strlen(LP3_PROVIDER_SUFFIX);

    return length > prefix_length + suffix_length &&
           strncmp(name, LP3_PROVIDER_PREFIX, prefix_length) == 0 &&
           strcmp(name + length - suffix_length, LP3_PROVIDER_SUFFIX) == 0;
}

static int secure_directory(const char *directory, int *directory_fd) {
    struct stat info;
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0 || fstat(fd, &info) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        set_snapshot_state("missing", "platform provider directory is unavailable");
        return 0;
    }
    if (!S_ISDIR(info.st_mode) || info.st_uid != 0 || info.st_gid != 0 ||
        (info.st_mode & 0022) != 0) {
        close(fd);
        set_snapshot_state("failed", "platform provider directory is unsafe");
        return 0;
    }
    *directory_fd = fd;
    return 1;
}

/* Require the exact shipped root:root 0755 provider mode. */
static int secure_provider_file(int directory_fd, const char *name) {
    struct stat info;
    if (fstatat(directory_fd, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        set_snapshot_state("failed", "cannot stat platform provider");
        return 0;
    }
    if (S_ISLNK(info.st_mode) || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        info.st_gid != 0 || (info.st_mode & 0777) != 0755) {
        set_snapshot_state("failed", "platform provider has unsafe ownership or mode");
        return 0;
    }
    return 1;
}

static int find_provider(char *name, size_t name_size) {
    DIR *directory;
    struct dirent *entry;
    int directory_fd;
    int count = 0;

    if (!secure_directory(LP3_PLATFORM_DIRECTORY, &directory_fd)) {
        return 0;
    }
    directory = fdopendir(directory_fd);
    if (directory == NULL) {
        close(directory_fd);
        set_snapshot_state("failed", "cannot read platform provider directory");
        return 0;
    }

    while ((entry = readdir(directory)) != NULL) {
        if (!candidate_name(entry->d_name)) {
            continue;
        }
        if (!secure_provider_file(directory_fd, entry->d_name)) {
            closedir(directory);
            return 0;
        }
        ++count;
        if (count > 1) {
            closedir(directory);
            set_snapshot_state("failed", "platform providers are ambiguous");
            return 0;
        }
        set_ascii_field(name, name_size, entry->d_name, strlen(entry->d_name));
    }
    closedir(directory);

    if (count == 0) {
        set_snapshot_state("missing", "no platform provider is installed");
        return 0;
    }
    return 1;
}

static int valid_time_state(const struct lp3_platform_time_state_v1 *time) {
    return time != NULL && time->struct_size >= sizeof(*time) &&
           time->utc_offset_seconds >= -24 * 60 * 60 &&
           time->utc_offset_seconds <= 24 * 60 * 60 &&
           time->is_24_hour <= 1;
}

static int valid_utf8(const char *text, uint32_t size) {
    uint32_t index = 0;
    if (size != 0 && text == NULL) {
        return 0;
    }
    while (index < size) {
        const unsigned char first = (unsigned char)text[index];
        if (first == 0) {
            return 0;
        }
        if (first <= 0x7f) {
            ++index;
        } else if (first >= 0xc2 && first <= 0xdf) {
            if (index + 1 >= size ||
                (((unsigned char)text[index + 1]) & 0xc0) != 0x80) {
                return 0;
            }
            index += 2;
        } else if (first >= 0xe0 && first <= 0xef) {
            unsigned char second;
            unsigned char third;
            if (index + 2 >= size) {
                return 0;
            }
            second = (unsigned char)text[index + 1];
            third = (unsigned char)text[index + 2];
            if ((third & 0xc0) != 0x80 ||
                (first == 0xe0 ? second < 0xa0 || second > 0xbf :
                 first == 0xed ? second < 0x80 || second > 0x9f :
                 (second & 0xc0) != 0x80)) {
                return 0;
            }
            index += 3;
        } else if (first >= 0xf0 && first <= 0xf4) {
            unsigned char second;
            unsigned char third;
            unsigned char fourth;
            if (index + 3 >= size) {
                return 0;
            }
            second = (unsigned char)text[index + 1];
            third = (unsigned char)text[index + 2];
            fourth = (unsigned char)text[index + 3];
            if ((third & 0xc0) != 0x80 || (fourth & 0xc0) != 0x80 ||
                (first == 0xf0 ? second < 0x90 || second > 0xbf :
                 first == 0xf4 ? second < 0x80 || second > 0x8f :
                 (second & 0xc0) != 0x80)) {
                return 0;
            }
            index += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static int java_string_to_utf8(JNIEnv *env, jstring value, char *output,
                               uint32_t maximum, uint32_t *output_size,
                               int allow_empty) {
    const jchar *characters;
    jsize length;
    uint32_t written = 0;
    jsize index;

    if (value == NULL || output == NULL || output_size == NULL) {
        return 0;
    }
    length = (*env)->GetStringLength(env, value);
    if ((!allow_empty && length == 0) || length > (jsize)maximum) {
        return 0;
    }
    characters = (*env)->GetStringChars(env, value, NULL);
    if (characters == NULL) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        uint32_t codepoint = characters[index];
        uint32_t needed;
        if (codepoint == 0 || (codepoint >= 0xdc00 && codepoint <= 0xdfff)) {
            (*env)->ReleaseStringChars(env, value, characters);
            return 0;
        }
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            uint32_t low;
            if (++index >= length || characters[index] < 0xdc00 ||
                characters[index] > 0xdfff) {
                (*env)->ReleaseStringChars(env, value, characters);
                return 0;
            }
            low = characters[index];
            codepoint = UINT32_C(0x10000) +
                ((codepoint - UINT32_C(0xd800)) << 10) +
                (low - UINT32_C(0xdc00));
        }
        needed = codepoint <= UINT32_C(0x7f) ? 1 :
            codepoint <= UINT32_C(0x7ff) ? 2 :
            codepoint <= UINT32_C(0xffff) ? 3 : 4;
        if (written > maximum - needed) {
            (*env)->ReleaseStringChars(env, value, characters);
            return 0;
        }
        if (needed == 1) {
            output[written++] = (char)codepoint;
        } else if (needed == 2) {
            output[written++] = (char)(UINT32_C(0xc0) | (codepoint >> 6));
            output[written++] = (char)(UINT32_C(0x80) | (codepoint & 0x3f));
        } else if (needed == 3) {
            output[written++] = (char)(UINT32_C(0xe0) | (codepoint >> 12));
            output[written++] = (char)(UINT32_C(0x80) |
                ((codepoint >> 6) & 0x3f));
            output[written++] = (char)(UINT32_C(0x80) | (codepoint & 0x3f));
        } else {
            output[written++] = (char)(UINT32_C(0xf0) | (codepoint >> 18));
            output[written++] = (char)(UINT32_C(0x80) |
                ((codepoint >> 12) & 0x3f));
            output[written++] = (char)(UINT32_C(0x80) |
                ((codepoint >> 6) & 0x3f));
            output[written++] = (char)(UINT32_C(0x80) | (codepoint & 0x3f));
        }
    }
    (*env)->ReleaseStringChars(env, value, characters);
    output[written] = '\0';
    *output_size = written;
    return allow_empty || written != 0;
}

static int valid_string(const struct lp3_platform_string *value,
                        uint32_t maximum, int allow_empty) {
    return value != NULL && value->size <= maximum &&
           (allow_empty || value->size != 0) &&
           valid_utf8(value->data, value->size);
}

static int valid_bytes(const struct lp3_platform_bytes *value,
                       uint32_t maximum) {
    return value != NULL && value->size <= maximum &&
           (value->size == 0 || value->data != NULL);
}

static int string_empty(const struct lp3_platform_string *value) {
    return value != NULL && value->size == 0;
}

static int valid_notification(
    uint32_t event_type,
    const struct lp3_platform_notification_v1 *notification) {
    if (notification == NULL ||
        notification->struct_size < sizeof(*notification) ||
        notification->reserved != 0 ||
        !valid_string(&notification->id,
                      LP3_PLATFORM_NOTIFICATION_ID_MAX, 0) ||
        notification->icon.size != 0) {
        return 0;
    }
    if (event_type == LP3_PLATFORM_EVENT_NOTIFICATION_CLOSED) {
        return notification->flags == 0 && notification->timestamp_ms == 0 &&
               notification->close_reason <= 4 &&
               string_empty(&notification->replaces_id) &&
               string_empty(&notification->application_id) &&
               string_empty(&notification->application_name) &&
               string_empty(&notification->title) &&
               string_empty(&notification->body) &&
               string_empty(&notification->category) &&
               string_empty(&notification->icon_name);
    }
    return event_type == LP3_PLATFORM_EVENT_NOTIFICATION &&
           (notification->flags &
            ~(LP3_PLATFORM_NOTIFICATION_HAS_DEFAULT_ACTION |
              LP3_PLATFORM_NOTIFICATION_HAS_REPLY_ACTION)) == 0 &&
           notification->close_reason == 0 &&
           valid_string(&notification->replaces_id,
                        LP3_PLATFORM_NOTIFICATION_ID_MAX, 1) &&
           valid_string(&notification->application_id,
                        LP3_PLATFORM_NOTIFICATION_APPLICATION_ID_MAX, 0) &&
           valid_string(&notification->application_name,
                        LP3_PLATFORM_NOTIFICATION_APPLICATION_NAME_MAX, 1) &&
           valid_string(&notification->title,
                        LP3_PLATFORM_NOTIFICATION_TITLE_MAX, 1) &&
           valid_string(&notification->body,
                        LP3_PLATFORM_NOTIFICATION_BODY_MAX, 1) &&
           valid_string(&notification->category,
                        LP3_PLATFORM_NOTIFICATION_CATEGORY_MAX, 1) &&
           valid_string(&notification->icon_name,
                        LP3_PLATFORM_NOTIFICATION_ICON_NAME_MAX, 1) &&
           (notification->title.size != 0 || notification->body.size != 0);
}

static int valid_call_id(const struct lp3_platform_string *value) {
    uint32_t index;
    if (!valid_string(value, LP3_PLATFORM_CALL_ID_MAX, 0)) {
        return 0;
    }
    for (index = 0; index < value->size; ++index) {
        const unsigned char character = (unsigned char)value->data[index];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return 0;
        }
    }
    return 1;
}

static int valid_call(const struct lp3_platform_call_state_v1 *call) {
    if (call == NULL || call->struct_size < sizeof(*call) ||
        call->state > LP3_PLATFORM_CALL_HELD || !valid_call_id(&call->id) ||
        !valid_string(&call->name, LP3_PLATFORM_CALL_NAME_MAX, 1) ||
        !valid_string(&call->number, LP3_PLATFORM_CALL_NUMBER_MAX, 1)) {
        return 0;
    }
    return call->state != LP3_PLATFORM_CALL_ENDED ||
           (string_empty(&call->name) && string_empty(&call->number));
}

static int valid_media(const struct lp3_platform_media_state_v1 *media) {
    return media != NULL && media->struct_size >= sizeof(*media) &&
           media->flags == LP3_PLATFORM_MEDIA_SYSTEM_VOLUME &&
           media->volume_percent >= 0 && media->volume_percent <= 100 &&
           string_empty(&media->title) && string_empty(&media->artist) &&
           string_empty(&media->album);
}

static int valid_location_status(int32_t status) {
    return status >= LP3_PLATFORM_OK && status <= LP3_PLATFORM_INTERNAL_ERROR;
}

static int valid_location(const struct lp3_platform_location_v1 *location) {
    return location != NULL && location->struct_size >= sizeof(*location) &&
           location->latitude_e7 >= -900000000 &&
           location->latitude_e7 <= 900000000 &&
           location->longitude_e7 >= -1800000000 &&
           location->longitude_e7 <= 1800000000 &&
           location->accuracy_m >= 0 && location->timestamp_ms > 0;
}

static int valid_provider_reset(
    const struct lp3_platform_provider_status_v1 *status) {
    uint64_t classified;
    if (status == NULL || status->struct_size < sizeof(*status) ||
        (status->state != LP3_PLATFORM_PROVIDER_DEGRADED &&
         status->state != LP3_PLATFORM_PROVIDER_FAILED) ||
        status->ready_domains != 0 || status->helper_pid != 0 ||
        status->error.size >= LP3_MAX_FIELD ||
        !valid_string(&status->error, LP3_MAX_FIELD - 1, 1)) {
        return 0;
    }
    classified = status->degraded_domains | status->failed_domains;
    return classified != 0 && (classified & ~LP3_PLATFORM_DOMAIN_ALL) == 0 &&
           (status->degraded_domains & status->failed_domains) == 0;
}

static void copy_string(char *destination, size_t destination_size,
                        const struct lp3_platform_string *source) {
    if (source->size != 0) {
        memcpy(destination, source->data, source->size);
    }
    destination[source->size < destination_size ? source->size :
        destination_size - 1] = '\0';
}

struct calendar_encoder {
    uint8_t *data;
    size_t size;
};

static int calendar_append(struct calendar_encoder *encoder,
                           const void *data, size_t size) {
    if (encoder->size > LP3_MAX_PAYLOAD_BYTES ||
        size > LP3_MAX_PAYLOAD_BYTES - encoder->size) {
        return 0;
    }
    if (size == 0) {
        return 1;
    }
    memcpy(encoder->data + encoder->size, data, size);
    encoder->size += size;
    return 1;
}

static int calendar_append_u32(struct calendar_encoder *encoder,
                               uint32_t value) {
    uint8_t data[4];
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
    return calendar_append(encoder, data, sizeof(data));
}

static int calendar_append_u64(struct calendar_encoder *encoder,
                               uint64_t value) {
    uint8_t data[8];
    size_t index;
    for (index = 0; index < sizeof(data); ++index) {
        data[index] = (uint8_t)(value >> (index * 8));
    }
    return calendar_append(encoder, data, sizeof(data));
}

static int calendar_append_string(
    struct calendar_encoder *encoder,
    const struct lp3_platform_string *value, uint32_t maximum, int allow_empty) {
    return valid_string(value, maximum, allow_empty) &&
           calendar_append_u32(encoder, value->size) &&
           calendar_append(encoder, value->data, value->size);
}

static uint8_t *encode_calendar_event(
    const struct lp3_platform_event_v1 *event, uint32_t *encoded_size) {
    const struct lp3_platform_calendar_snapshot_v1 *snapshot =
        event->calendar_snapshot;
    struct calendar_encoder encoder;
    uint32_t calendar_count = 0;
    uint32_t event_count_value = 0;
    uint32_t kind = 0;
    uint32_t next_offset = 0;
    uint32_t index;

    if (encoded_size == NULL ||
        event->struct_size <
            offsetof(struct lp3_platform_event_v1, calendar_snapshot) +
                sizeof(event->calendar_snapshot) ||
        !valid_location_status(event->status)) {
        return NULL;
    }
    if (event->request_id == 0) {
        if (event->status != LP3_PLATFORM_OK || snapshot != NULL) {
            return NULL;
        }
    } else if (event->status == LP3_PLATFORM_OK) {
        if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot) ||
            (snapshot->kind != LP3_PLATFORM_CALENDAR_QUERY_CALENDARS &&
             snapshot->kind != LP3_PLATFORM_CALENDAR_QUERY_EVENTS) ||
            snapshot->next_offset > 512 ||
            snapshot->calendar_count > LP3_PLATFORM_CALENDAR_PAGE_MAX ||
            snapshot->event_count > LP3_PLATFORM_CALENDAR_PAGE_MAX ||
            (snapshot->kind == LP3_PLATFORM_CALENDAR_QUERY_CALENDARS &&
             snapshot->event_count != 0) ||
            (snapshot->kind == LP3_PLATFORM_CALENDAR_QUERY_EVENTS &&
             snapshot->calendar_count != 0) ||
            (snapshot->calendar_count != 0 && snapshot->calendars == NULL) ||
            (snapshot->event_count != 0 && snapshot->events == NULL)) {
            return NULL;
        }
        kind = snapshot->kind;
        next_offset = snapshot->next_offset;
        calendar_count = snapshot->calendar_count;
        event_count_value = snapshot->event_count;
    } else if (snapshot != NULL) {
        return NULL;
    }

    encoder.data = (uint8_t *)malloc(LP3_MAX_PAYLOAD_BYTES);
    encoder.size = 0;
    if (encoder.data == NULL ||
        !calendar_append_u64(&encoder, event->request_id) ||
        !calendar_append_u32(&encoder, (uint32_t)event->status) ||
        !calendar_append_u32(&encoder, kind) ||
        !calendar_append_u32(&encoder, next_offset) ||
        !calendar_append_u32(&encoder, calendar_count) ||
        !calendar_append_u32(&encoder, event_count_value) ||
        !calendar_append_u32(&encoder, event->request_id == 0 ? 1 : 0)) {
        free(encoder.data);
        return NULL;
    }
    for (index = 0; index < calendar_count; ++index) {
        const struct lp3_platform_calendar_v1 *calendar =
            &snapshot->calendars[index];
        if (calendar->struct_size < sizeof(*calendar) ||
            calendar->reserved != 0 ||
            (calendar->flags & ~(LP3_PLATFORM_CALENDAR_VISIBLE |
                                 LP3_PLATFORM_CALENDAR_ENABLED |
                                 LP3_PLATFORM_CALENDAR_SYNC_EVENTS)) != 0 ||
            !calendar_append_u32(&encoder, calendar->flags) ||
            !calendar_append_u32(&encoder, calendar->color_argb) ||
            !calendar_append_string(&encoder, &calendar->id,
                                    LP3_PLATFORM_CALENDAR_ID_MAX, 0) ||
            !calendar_append_string(&encoder, &calendar->name,
                                    LP3_PLATFORM_CALENDAR_NAME_MAX, 0) ||
            !calendar_append_string(&encoder, &calendar->owner_name,
                                    LP3_PLATFORM_CALENDAR_OWNER_MAX, 1) ||
            !calendar_append_string(&encoder, &calendar->owner_id,
                                    LP3_PLATFORM_CALENDAR_OWNER_MAX, 1)) {
            free(encoder.data);
            return NULL;
        }
    }
    for (index = 0; index < event_count_value; ++index) {
        const struct lp3_platform_calendar_event_v1 *calendar_event =
            &snapshot->events[index];
        uint32_t attendee_index;
        uint32_t reminder_index;
        if (calendar_event->struct_size < sizeof(*calendar_event) ||
            (calendar_event->flags &
             ~(LP3_PLATFORM_CALENDAR_EVENT_ALL_DAY |
               LP3_PLATFORM_CALENDAR_EVENT_RECURS)) != 0 ||
            calendar_event->availability > 3 || calendar_event->status > 3 ||
            calendar_event->start_ms >= calendar_event->end_ms ||
            calendar_event->attendee_count >
                LP3_PLATFORM_CALENDAR_ATTENDEE_MAX ||
            calendar_event->reminder_count >
                LP3_PLATFORM_CALENDAR_REMINDER_MAX ||
            (calendar_event->attendee_count != 0 &&
             calendar_event->attendees == NULL) ||
            (calendar_event->reminder_count != 0 &&
             calendar_event->reminder_minutes == NULL) ||
            !calendar_append_u32(&encoder, calendar_event->flags) ||
            !calendar_append_u32(&encoder, calendar_event->availability) ||
            !calendar_append_u32(&encoder, calendar_event->status) ||
            !calendar_append_u32(&encoder, calendar_event->attendee_count) ||
            !calendar_append_u32(&encoder, calendar_event->reminder_count) ||
            !calendar_append_u64(&encoder, (uint64_t)calendar_event->start_ms) ||
            !calendar_append_u64(&encoder, (uint64_t)calendar_event->end_ms) ||
            !calendar_append_string(&encoder, &calendar_event->id,
                                    LP3_PLATFORM_CALENDAR_EVENT_ID_MAX, 0) ||
            !calendar_append_string(&encoder, &calendar_event->calendar_id,
                                    LP3_PLATFORM_CALENDAR_ID_MAX, 0) ||
            !calendar_append_string(&encoder, &calendar_event->base_event_id,
                                    LP3_PLATFORM_CALENDAR_EVENT_ID_MAX, 0) ||
            !calendar_append_string(&encoder, &calendar_event->title,
                                    LP3_PLATFORM_CALENDAR_TITLE_MAX, 0) ||
            !calendar_append_string(&encoder, &calendar_event->description,
                                    LP3_PLATFORM_CALENDAR_DESCRIPTION_MAX, 1) ||
            !calendar_append_string(&encoder, &calendar_event->location,
                                    LP3_PLATFORM_CALENDAR_LOCATION_MAX, 1)) {
            free(encoder.data);
            return NULL;
        }
        for (attendee_index = 0;
             attendee_index < calendar_event->attendee_count;
             ++attendee_index) {
            const struct lp3_platform_calendar_attendee_v1 *attendee =
                &calendar_event->attendees[attendee_index];
            if (attendee->struct_size < sizeof(*attendee) ||
                (attendee->flags &
                 ~(LP3_PLATFORM_CALENDAR_ATTENDEE_ORGANIZER |
                   LP3_PLATFORM_CALENDAR_ATTENDEE_CURRENT_USER)) != 0 ||
                attendee->role > 3 || attendee->status > 4 ||
                (attendee->name.size == 0 && attendee->email.size == 0) ||
                !calendar_append_u32(&encoder, attendee->flags) ||
                !calendar_append_u32(&encoder, attendee->role) ||
                !calendar_append_u32(&encoder, attendee->status) ||
                !calendar_append_string(&encoder, &attendee->name,
                                        LP3_PLATFORM_CALENDAR_OWNER_MAX, 1) ||
                !calendar_append_string(&encoder, &attendee->email,
                                        LP3_PLATFORM_CALENDAR_OWNER_MAX, 1)) {
                free(encoder.data);
                return NULL;
            }
        }
        for (reminder_index = 0;
             reminder_index < calendar_event->reminder_count;
             ++reminder_index) {
            int32_t minutes = calendar_event->reminder_minutes[reminder_index];
            if (minutes < 0 || minutes > 366 * 24 * 60 ||
                !calendar_append_u32(&encoder, (uint32_t)minutes)) {
                free(encoder.data);
                return NULL;
            }
        }
    }
    *encoded_size = (uint32_t)encoder.size;
    return encoder.data;
}

static uint8_t *encode_contact_event(
    const struct lp3_platform_event_v1 *event, uint32_t *encoded_size) {
    const struct lp3_platform_contact_snapshot_v1 *snapshot =
        event->contact_snapshot;
    struct calendar_encoder encoder;
    uint32_t kind = 0;
    uint32_t next_offset = 0;
    uint32_t contact_count = 0;
    uint32_t index;

    if (encoded_size == NULL ||
        event->struct_size <
            offsetof(struct lp3_platform_event_v1, contact_snapshot) +
                sizeof(event->contact_snapshot) ||
        !valid_location_status(event->status)) {
        return NULL;
    }
    if (event->request_id == 0) {
        if (event->status != LP3_PLATFORM_OK || snapshot != NULL) return NULL;
    } else if (event->status == LP3_PLATFORM_OK) {
        if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot) ||
            (snapshot->kind != LP3_PLATFORM_CONTACT_QUERY_LIST &&
             snapshot->kind != LP3_PLATFORM_CONTACT_QUERY_PHONE) ||
            snapshot->next_offset > 4096 ||
            snapshot->contact_count > LP3_PLATFORM_CONTACT_PAGE_MAX ||
            (snapshot->kind == LP3_PLATFORM_CONTACT_QUERY_PHONE &&
             (snapshot->next_offset != 0 || snapshot->contact_count > 1)) ||
            (snapshot->contact_count != 0 && snapshot->contacts == NULL)) {
            return NULL;
        }
        kind = snapshot->kind;
        next_offset = snapshot->next_offset;
        contact_count = snapshot->contact_count;
    } else if (snapshot != NULL) {
        return NULL;
    }

    encoder.data = (uint8_t *)malloc(LP3_MAX_PAYLOAD_BYTES);
    encoder.size = 0;
    if (encoder.data == NULL ||
        !calendar_append_u64(&encoder, event->request_id) ||
        !calendar_append_u32(&encoder, (uint32_t)event->status) ||
        !calendar_append_u32(&encoder, kind) ||
        !calendar_append_u32(&encoder, next_offset) ||
        !calendar_append_u32(&encoder, contact_count) ||
        !calendar_append_u32(&encoder, event->request_id == 0 ? 1 : 0)) {
        free(encoder.data);
        return NULL;
    }
    for (index = 0; index < contact_count; ++index) {
        const struct lp3_platform_contact_v1 *contact =
            &snapshot->contacts[index];
        if (contact->struct_size < sizeof(*contact) || contact->flags != 0 ||
            !valid_bytes(&contact->avatar, LP3_PLATFORM_CONTACT_AVATAR_MAX) ||
            !calendar_append_u32(&encoder, contact->flags) ||
            !calendar_append_string(&encoder, &contact->id,
                                    LP3_PLATFORM_CONTACT_ID_MAX, 0) ||
            !calendar_append_string(&encoder, &contact->display_name,
                                    LP3_PLATFORM_CONTACT_NAME_MAX, 0) ||
            !calendar_append_string(&encoder, &contact->phone_number,
                                    LP3_PLATFORM_CONTACT_NUMBER_MAX, 1) ||
            !calendar_append_u32(&encoder, contact->avatar.size) ||
            !calendar_append(&encoder, contact->avatar.data,
                             contact->avatar.size)) {
            free(encoder.data);
            return NULL;
        }
    }
    *encoded_size = (uint32_t)encoder.size;
    return encoder.data;
}

static void reset_events(void) {
    lock_event_queues();
    memset(event_queue, 0, sizeof(event_queue));
    event_head = 0;
    event_count = 0;
    memset(notification_event_queue, 0, sizeof(notification_event_queue));
    notification_event_head = 0;
    notification_event_count = 0;
    memset(call_event_queue, 0, sizeof(call_event_queue));
    call_event_head = 0;
    call_event_count = 0;
    memset(media_event_queue, 0, sizeof(media_event_queue));
    media_event_head = 0;
    media_event_count = 0;
    clear_location_events();
    clear_calendar_events();
    clear_contact_events();
    event_failed_domains = 0;
    provider_reset_pending = 0;
    unlock_event_queues();
}

static uint64_t failed_event_domains(void) {
    uint64_t domains;
    lock_event_queues();
    domains = event_failed_domains;
    unlock_event_queues();
    return domains;
}

/* Copy typed provider events; provider-owned pointers never cross JNI. */
static void provider_event(void *context, const struct lp3_platform_event_v1 *event) {
    (void)context;

    lock_event_queues();
    if (event != NULL && event->type == LP3_PLATFORM_EVENT_PROVIDER_STATUS) {
        while (provider_command_dispatches != 0) {
            pthread_cond_wait(&provider_command_condition, &event_lock);
        }
    }
    if (event == NULL ||
        event->struct_size < offsetof(struct lp3_platform_event_v1, notification) +
            sizeof(event->notification) ||
        event->reserved != 0) {
        event_failed_domains = LP3_PLATFORM_DOMAIN_ALL;
        event_head = 0;
        event_count = 0;
        notification_event_head = 0;
        notification_event_count = 0;
        call_event_head = 0;
        call_event_count = 0;
        media_event_head = 0;
        media_event_count = 0;
        clear_location_events();
        clear_calendar_events();
        clear_contact_events();
        unlock_event_queues();
        return;
    }
    if (event->type != LP3_PLATFORM_EVENT_LOCATION &&
        event->type != LP3_PLATFORM_EVENT_CALENDAR &&
        event->type != LP3_PLATFORM_EVENT_CONTACT &&
        (event->request_id != 0 || event->status != LP3_PLATFORM_OK)) {
        event_failed_domains = LP3_PLATFORM_DOMAIN_ALL;
        event_head = notification_event_head = call_event_head = media_event_head = 0;
        event_count = notification_event_count = call_event_count = media_event_count = 0;
        clear_location_events();
        clear_calendar_events();
        clear_contact_events();
        unlock_event_queues();
        return;
    }
    if (event->type == LP3_PLATFORM_EVENT_TIME_CHANGED) {
        struct lp3_copied_event *copied;
        if (event->struct_size < offsetof(struct lp3_platform_event_v1, time) +
                sizeof(event->time) || !valid_time_state(event->time) ||
            event_count + notification_event_count + call_event_count +
                media_event_count + location_event_count +
                calendar_event_count + contact_event_count >=
                LP3_MAX_QUEUED_EVENTS) {
            event_failed_domains |= LP3_PLATFORM_DOMAIN_TIME;
            event_head = 0;
            event_count = 0;
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_TIME) != 0) {
            unlock_event_queues();
            return;
        }
        copied = &event_queue[(event_head + event_count) %
            LP3_MAX_QUEUED_EVENTS];
        copied->type = event->type;
        copied->utc_offset_seconds = event->time->utc_offset_seconds;
        copied->unix_ms = event->time->unix_ms;
        copied->is_24_hour = event->time->is_24_hour;
        ++event_count;
    } else if (event->type == LP3_PLATFORM_EVENT_NOTIFICATION ||
               event->type == LP3_PLATFORM_EVENT_NOTIFICATION_CLOSED) {
        struct lp3_copied_notification_event *copied;
        if (!valid_notification(event->type, event->notification) ||
            event_count + notification_event_count + call_event_count +
                media_event_count + location_event_count +
                calendar_event_count + contact_event_count >=
                LP3_MAX_QUEUED_EVENTS) {
            event_failed_domains |= LP3_PLATFORM_DOMAIN_NOTIFICATIONS;
            notification_event_head = 0;
            notification_event_count = 0;
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_NOTIFICATIONS) != 0) {
            unlock_event_queues();
            return;
        }
        copied = &notification_event_queue[
            (notification_event_head + notification_event_count) %
                LP3_MAX_QUEUED_EVENTS];
        memset(copied, 0, sizeof(*copied));
        copied->type = event->type;
        copied->flags = event->notification->flags;
        copied->timestamp_ms = event->notification->timestamp_ms;
        copied->close_reason = event->notification->close_reason;
        copy_string(copied->id, sizeof(copied->id),
                    &event->notification->id);
        copy_string(copied->replaces_id, sizeof(copied->replaces_id),
                    &event->notification->replaces_id);
        copy_string(copied->application_id, sizeof(copied->application_id),
                    &event->notification->application_id);
        copy_string(copied->application_name,
                    sizeof(copied->application_name),
                    &event->notification->application_name);
        copy_string(copied->title, sizeof(copied->title),
                    &event->notification->title);
        copy_string(copied->body, sizeof(copied->body),
                    &event->notification->body);
        copy_string(copied->category, sizeof(copied->category),
                    &event->notification->category);
        copy_string(copied->icon_name, sizeof(copied->icon_name),
                    &event->notification->icon_name);
        ++notification_event_count;
    } else if (event->type == LP3_PLATFORM_EVENT_CALL) {
        struct lp3_copied_call_event *copied;
        if (event->struct_size < offsetof(struct lp3_platform_event_v1, call) +
                sizeof(event->call) || !valid_call(event->call) ||
            event_count + notification_event_count + call_event_count +
                media_event_count + location_event_count +
                calendar_event_count + contact_event_count >=
                LP3_MAX_QUEUED_EVENTS) {
            event_failed_domains |= LP3_PLATFORM_DOMAIN_CALLS;
            call_event_head = 0;
            call_event_count = 0;
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_CALLS) != 0) {
            unlock_event_queues();
            return;
        }
        copied = &call_event_queue[
            (call_event_head + call_event_count) % LP3_MAX_QUEUED_EVENTS];
        memset(copied, 0, sizeof(*copied));
        copied->state = event->call->state;
        copy_string(copied->id, sizeof(copied->id), &event->call->id);
        copy_string(copied->name, sizeof(copied->name), &event->call->name);
        copy_string(copied->number, sizeof(copied->number), &event->call->number);
        ++call_event_count;
    } else if (event->type == LP3_PLATFORM_EVENT_MEDIA) {
        struct lp3_copied_media_event *copied;
        if (event->struct_size < offsetof(struct lp3_platform_event_v1, media) +
                sizeof(event->media) || !valid_media(event->media)) {
            event_failed_domains |= LP3_PLATFORM_DOMAIN_MEDIA;
            media_event_head = 0;
            media_event_count = 0;
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_MEDIA) != 0) {
            unlock_event_queues();
            return;
        }
        if (media_event_count == 0) {
            if (event_count + notification_event_count + call_event_count +
                    location_event_count + calendar_event_count +
                    contact_event_count >= LP3_MAX_QUEUED_EVENTS) {
                event_failed_domains |= LP3_PLATFORM_DOMAIN_MEDIA;
                unlock_event_queues();
                return;
            }
            copied = &media_event_queue[media_event_head];
            media_event_count = 1;
        } else {
            /* System volume is level-triggered; retain only its latest value. */
            copied = &media_event_queue[
                (media_event_head + media_event_count - 1) %
                    LP3_MAX_QUEUED_EVENTS];
        }
        copied->flags = event->media->flags;
        copied->volume_percent = event->media->volume_percent;
    } else if (event->type == LP3_PLATFORM_EVENT_LOCATION) {
        struct lp3_copied_location_event *copied;
        int outstanding_index;
        int tombstone_index;

        if (event->struct_size < offsetof(struct lp3_platform_event_v1, location) +
                sizeof(event->location) || event->request_id == 0) {
            event_failed_domains |= LP3_PLATFORM_DOMAIN_LOCATION;
            clear_location_events();
            unlock_event_queues();
            return;
        }
        tombstone_index = location_id_index(location_tombstones,
                                             location_tombstone_count,
                                             event->request_id);
        if (tombstone_index >= 0) {
            location_remove_id(location_tombstones, &location_tombstone_count,
                               (size_t)tombstone_index);
            unlock_event_queues();
            return;
        }
        outstanding_index = location_id_index(location_outstanding,
                                               location_outstanding_count,
                                               event->request_id);
        /* Unknown and retired completions have no authority over this domain. */
        if (outstanding_index < 0) {
            unlock_event_queues();
            return;
        }
        if (!valid_location_status(event->status) ||
            (event->status == LP3_PLATFORM_OK && !valid_location(event->location)) ||
            (event->status != LP3_PLATFORM_OK && event->location != NULL)) {
            event_failed_domains |= LP3_PLATFORM_DOMAIN_LOCATION;
            clear_location_events();
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_LOCATION) != 0 ||
            location_event_count >= LP3_MAX_QUEUED_EVENTS ||
            event_count + notification_event_count + call_event_count +
                media_event_count + location_event_count +
                    calendar_event_count + contact_event_count >=
                    LP3_MAX_QUEUED_EVENTS) {
            if (location_event_count >= LP3_MAX_QUEUED_EVENTS ||
                event_count + notification_event_count + call_event_count +
                    media_event_count + location_event_count +
                        calendar_event_count + contact_event_count >=
                        LP3_MAX_QUEUED_EVENTS) {
                event_failed_domains |= LP3_PLATFORM_DOMAIN_LOCATION;
                clear_location_events();
            }
            unlock_event_queues();
            return;
        }
        location_remove_id(location_outstanding, &location_outstanding_count,
                           (size_t)outstanding_index);
        copied = &location_event_queue[(location_event_head + location_event_count) %
                                       LP3_MAX_QUEUED_EVENTS];
        memset(copied, 0, sizeof(*copied));
        copied->request_id = event->request_id;
        copied->status = event->status;
        if (event->status == LP3_PLATFORM_OK) {
            copied->latitude_e7 = event->location->latitude_e7;
            copied->longitude_e7 = event->location->longitude_e7;
            copied->accuracy_m = event->location->accuracy_m;
            copied->timestamp_ms = event->location->timestamp_ms;
        }
        ++location_event_count;
    } else if (event->type == LP3_PLATFORM_EVENT_CALENDAR) {
        struct lp3_copied_calendar_event *copied;
        uint8_t *payload;
        uint32_t payload_size = 0;
        int outstanding_index = -1;
        int tombstone_index;

        if (event->request_id != 0) {
            tombstone_index = location_id_index(
                calendar_tombstones, calendar_tombstone_count,
                event->request_id);
            if (tombstone_index >= 0) {
                location_remove_id(calendar_tombstones,
                                   &calendar_tombstone_count,
                                   (size_t)tombstone_index);
                unlock_event_queues();
                return;
            }
            outstanding_index = location_id_index(
                calendar_outstanding, calendar_outstanding_count,
                event->request_id);
            if (outstanding_index < 0) {
                unlock_event_queues();
                return;
            }
        }
        payload = encode_calendar_event(event, &payload_size);
        if (payload == NULL ||
            event_count + notification_event_count + call_event_count +
                media_event_count + location_event_count +
                calendar_event_count + contact_event_count >=
                LP3_MAX_QUEUED_EVENTS) {
            free(payload);
            event_failed_domains |= LP3_PLATFORM_DOMAIN_CALENDAR;
            clear_calendar_events();
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_CALENDAR) != 0) {
            free(payload);
            unlock_event_queues();
            return;
        }
        if (outstanding_index >= 0) {
            location_remove_id(calendar_outstanding,
                               &calendar_outstanding_count,
                               (size_t)outstanding_index);
        }
        copied = &calendar_event_queue[
            (calendar_event_head + calendar_event_count) %
                LP3_MAX_QUEUED_EVENTS];
        copied->payload = payload;
        copied->size = payload_size;
        ++calendar_event_count;
    } else if (event->type == LP3_PLATFORM_EVENT_CONTACT) {
        struct lp3_copied_contact_event *copied;
        uint8_t *payload;
        uint32_t payload_size = 0;
        int outstanding_index = -1;
        int tombstone_index;

        if (event->request_id != 0) {
            tombstone_index = location_id_index(
                contact_tombstones, contact_tombstone_count,
                event->request_id);
            if (tombstone_index >= 0) {
                location_remove_id(contact_tombstones,
                                   &contact_tombstone_count,
                                   (size_t)tombstone_index);
                unlock_event_queues();
                return;
            }
            outstanding_index = location_id_index(
                contact_outstanding, contact_outstanding_count,
                event->request_id);
            if (outstanding_index < 0) {
                unlock_event_queues();
                return;
            }
        }
        payload = encode_contact_event(event, &payload_size);
        if (payload == NULL ||
            event_count + notification_event_count + call_event_count +
                media_event_count + location_event_count +
                calendar_event_count + contact_event_count >=
                LP3_MAX_QUEUED_EVENTS) {
            free(payload);
            event_failed_domains |= LP3_PLATFORM_DOMAIN_CONTACTS;
            clear_contact_events();
            unlock_event_queues();
            return;
        }
        if ((event_failed_domains & LP3_PLATFORM_DOMAIN_CONTACTS) != 0) {
            free(payload);
            unlock_event_queues();
            return;
        }
        if (outstanding_index >= 0) {
            location_remove_id(contact_outstanding,
                               &contact_outstanding_count,
                               (size_t)outstanding_index);
        }
        copied = &contact_event_queue[
            (contact_event_head + contact_event_count) %
                LP3_MAX_QUEUED_EVENTS];
        copied->payload = payload;
        copied->size = payload_size;
        ++contact_event_count;
    } else if (event->type == LP3_PLATFORM_EVENT_PROVIDER_STATUS) {
        if (event->struct_size <
                offsetof(struct lp3_platform_event_v1, provider_status) +
                    sizeof(event->provider_status) ||
            !valid_provider_reset(event->provider_status)) {
            event_failed_domains = LP3_PLATFORM_DOMAIN_ALL;
            provider_reset_pending = 0;
        } else {
            /*
             * This is a generation barrier from the proxy. Discard every
             * event copied from the dead helper before allowing events from
             * its replacement to enter the queues.
             */
            event_failed_domains = 0;
            provider_reset_pending = 1;
        }
        event_head = 0;
        event_count = 0;
        notification_event_head = 0;
        notification_event_count = 0;
        call_event_head = 0;
        call_event_count = 0;
        media_event_head = 0;
        media_event_count = 0;
        clear_location_events();
        clear_calendar_events();
        clear_contact_events();
    } else {
        event_failed_domains = LP3_PLATFORM_DOMAIN_ALL;
        provider_reset_pending = 0;
        event_head = 0;
        event_count = 0;
        notification_event_head = 0;
        notification_event_count = 0;
        call_event_head = 0;
        call_event_count = 0;
        media_event_head = 0;
        media_event_count = 0;
        clear_location_events();
        clear_calendar_events();
        clear_contact_events();
    }
    unlock_event_queues();
}

#define API_HAS_MEMBER(api, member) \
    ((api)->struct_size >= offsetof(struct lp3_platform_api_v1, member) + \
        sizeof((api)->member) && (api)->member != NULL)

static int api_is_compatible(const struct lp3_platform_api_v1 *api) {
    size_t minimum_api_size = offsetof(struct lp3_platform_api_v1, destroy) +
                              sizeof(api->destroy);
    size_t minimum_info_size = offsetof(struct lp3_platform_provider_info_v1, build_id) +
                               sizeof(api->info.build_id);
    uint64_t domains;

    if (api == NULL || api->struct_size < minimum_api_size ||
        api->info.struct_size < minimum_info_size ||
        api->info.abi_major != LP3_PLATFORM_ABI_MAJOR ||
        api->info.abi_minor > LP3_PLATFORM_ABI_MINOR ||
        !API_HAS_MEMBER(api, probe) || !API_HAS_MEMBER(api, create) ||
        !API_HAS_MEMBER(api, start) || !API_HAS_MEMBER(api, cancel) ||
        !API_HAS_MEMBER(api, request_stop) || !API_HAS_MEMBER(api, destroy) ||
        !API_HAS_MEMBER(api, get_status)) {
        return 0;
    }
    domains = api->info.domains;
    if ((domains & ~LP3_PLATFORM_DOMAIN_ALL) != 0 ||
        ((domains & LP3_PLATFORM_DOMAIN_NOTIFICATIONS) != 0 &&
         !API_HAS_MEMBER(api, notification_command)) ||
        ((domains & LP3_PLATFORM_DOMAIN_MESSAGING) != 0 &&
         !API_HAS_MEMBER(api, reply_message)) ||
        ((domains & LP3_PLATFORM_DOMAIN_MEDIA) != 0 &&
         !API_HAS_MEMBER(api, media_command)) ||
        ((domains & LP3_PLATFORM_DOMAIN_CALLS) != 0 &&
         !API_HAS_MEMBER(api, call_command)) ||
        ((domains & LP3_PLATFORM_DOMAIN_CALENDAR) != 0 &&
         (api->info.abi_minor < 5 ||
          !API_HAS_MEMBER(api, calendar_query))) ||
        ((domains & LP3_PLATFORM_DOMAIN_CONTACTS) != 0 &&
         (api->info.abi_minor < 6 ||
          !API_HAS_MEMBER(api, contact_query))) ||
        ((domains & LP3_PLATFORM_DOMAIN_LOCATION) != 0 &&
         !API_HAS_MEMBER(api, location_query)) ||
        ((domains & LP3_PLATFORM_DOMAIN_TIME) != 0 &&
         !API_HAS_MEMBER(api, get_time_state)) ||
        ((domains & LP3_PLATFORM_DOMAIN_DEVICE_STATE) != 0 &&
         !API_HAS_MEMBER(api, get_device_state)) ||
        ((domains & LP3_PLATFORM_DOMAIN_PROFILES) != 0 &&
         !API_HAS_MEMBER(api, set_profile))) {
        return 0;
    }
    return 1;
}

static void set_provider_snapshot_base(const struct lp3_platform_api_v1 *api) {
    set_snapshot_state("ready", "");
    set_ascii_field(loader.snapshot.provider, sizeof(loader.snapshot.provider),
                    api->info.provider_name.data, api->info.provider_name.size);
    set_ascii_field(loader.snapshot.build_id, sizeof(loader.snapshot.build_id),
                    api->info.build_id.data, api->info.build_id.size);
    snprintf(loader.snapshot.abi_version, sizeof(loader.snapshot.abi_version), "%u.%u",
             api->info.abi_major, api->info.abi_minor);
    snprintf(loader.snapshot.supported_domains,
             sizeof(loader.snapshot.supported_domains), "%llu",
             (unsigned long long)api->info.domains);
}

static void refresh_status_locked(void) {
    struct lp3_platform_provider_status_v1 status;
    uint64_t event_failures;
    int32_t result;
    const char *state = "degraded";

    if (loader.api == NULL || loader.instance == NULL) {
        return;
    }
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    result = loader.api->get_status(loader.instance, &status);
    if (result != LP3_PLATFORM_OK) {
        set_literal(loader.snapshot.state, sizeof(loader.snapshot.state), "degraded");
        set_literal(loader.snapshot.error, sizeof(loader.snapshot.error),
                    "platform provider health is unavailable");
        set_literal(loader.snapshot.domains, sizeof(loader.snapshot.domains), "0");
        set_literal(loader.snapshot.degraded_domains,
                    sizeof(loader.snapshot.degraded_domains),
                    loader.snapshot.supported_domains);
        set_literal(loader.snapshot.failed_domains,
                    sizeof(loader.snapshot.failed_domains), "0");
        set_literal(loader.snapshot.helper_pid, sizeof(loader.snapshot.helper_pid), "0");
        return;
    }
    if ((status.state != LP3_PLATFORM_PROVIDER_READY &&
         status.state != LP3_PLATFORM_PROVIDER_DEGRADED &&
         status.state != LP3_PLATFORM_PROVIDER_FAILED) ||
        ((status.ready_domains | status.degraded_domains | status.failed_domains) &
         ~loader.api->info.domains) != 0 ||
        (status.ready_domains & status.degraded_domains) != 0 ||
        (status.ready_domains & status.failed_domains) != 0 ||
        (status.degraded_domains & status.failed_domains) != 0 ||
        status.helper_pid < 0 || status.error.size >= LP3_MAX_FIELD ||
        (status.error.size != 0 && status.error.data == NULL)) {
        set_literal(loader.snapshot.state, sizeof(loader.snapshot.state), "failed");
        set_literal(loader.snapshot.error, sizeof(loader.snapshot.error),
                    "platform provider returned invalid health data");
        set_literal(loader.snapshot.domains, sizeof(loader.snapshot.domains), "0");
        set_literal(loader.snapshot.degraded_domains,
                    sizeof(loader.snapshot.degraded_domains), "0");
        set_literal(loader.snapshot.failed_domains,
                    sizeof(loader.snapshot.failed_domains),
                    loader.snapshot.supported_domains);
        set_literal(loader.snapshot.helper_pid, sizeof(loader.snapshot.helper_pid), "0");
        return;
    }
    event_failures = failed_event_domains() & loader.api->info.domains;
    if (event_failures != 0) {
        status.ready_domains &= ~event_failures;
        status.degraded_domains |= event_failures & ~status.failed_domains;
        if (status.state == LP3_PLATFORM_PROVIDER_READY) {
            status.state = LP3_PLATFORM_PROVIDER_DEGRADED;
        }
    }
    if (status.state == LP3_PLATFORM_PROVIDER_READY) {
        state = "ready";
    } else if (status.state == LP3_PLATFORM_PROVIDER_FAILED) {
        state = "failed";
    }
    set_literal(loader.snapshot.state, sizeof(loader.snapshot.state), state);
    if (event_failures != 0) {
        set_literal(loader.snapshot.error, sizeof(loader.snapshot.error),
                    "platform provider event queue failed");
    } else {
        set_ascii_field(loader.snapshot.error, sizeof(loader.snapshot.error),
                        status.error.data, status.error.size);
    }
    snprintf(loader.snapshot.helper_pid, sizeof(loader.snapshot.helper_pid), "%lld",
             status.helper_pid > 0 ? (long long)status.helper_pid : 0LL);
    snprintf(loader.snapshot.domains, sizeof(loader.snapshot.domains), "%llu",
             (unsigned long long)status.ready_domains);
    snprintf(loader.snapshot.degraded_domains,
             sizeof(loader.snapshot.degraded_domains), "%llu",
             (unsigned long long)status.degraded_domains);
    snprintf(loader.snapshot.failed_domains,
             sizeof(loader.snapshot.failed_domains), "%llu",
             (unsigned long long)status.failed_domains);
}

/* Returns false rather than dlclose a provider that has not stopped safely. */
static int stop_locked(void) {
    if (loader.api != NULL && loader.instance != NULL) {
        if (loader.api->request_stop(loader.instance) != LP3_PLATFORM_OK) {
            set_literal(loader.snapshot.state, sizeof(loader.snapshot.state), "failed");
            set_literal(loader.snapshot.error, sizeof(loader.snapshot.error),
                        "platform provider did not stop safely");
            return 0;
        }
        loader.api->destroy(loader.instance);
    }
    if (loader.library != NULL) {
        dlclose(loader.library);
    }
    loader.library = NULL;
    loader.api = NULL;
    loader.instance = NULL;
    loader.next_request_id = 1;
    reset_events();
    set_snapshot_state("missing", "no platform provider is installed");
    return 1;
}

static void start_locked(void) {
    char name[NAME_MAX + 1];
    char fd_path[64];
    const struct lp3_platform_api_v1 *api = NULL;
    struct lp3_platform_instance *instance = NULL;
    struct lp3_platform_host_v1 host;
    lp3_get_api_fn get_api;
    int directory_fd;
    int provider_fd;
    int32_t status;

    if (!stop_locked()) {
        return;
    }
    reset_events();
    memset(name, 0, sizeof(name));
    if (!find_provider(name, sizeof(name))) {
        return;
    }

    if (!secure_directory(LP3_PLATFORM_DIRECTORY, &directory_fd)) {
        return;
    }
    provider_fd = openat(directory_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    close(directory_fd);
    if (provider_fd < 0) {
        set_snapshot_state("failed", "cannot open platform provider");
        return;
    }
    /* Revalidate the opened descriptor to make a directory-entry replacement fail closed. */
    {
        struct stat info;
        if (fstat(provider_fd, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_uid != 0 || info.st_gid != 0 || (info.st_mode & 0777) != 0755) {
            close(provider_fd);
            set_snapshot_state("failed", "platform provider changed during validation");
            return;
        }
    }
    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", provider_fd);
    loader.library = dlopen(fd_path, RTLD_NOW | RTLD_LOCAL);
    close(provider_fd);
    if (loader.library == NULL) {
        set_snapshot_state("failed", "cannot load platform provider");
        return;
    }

    dlerror();
    get_api = (lp3_get_api_fn)dlsym(loader.library, "lp3_platform_get_api");
    if (dlerror() != NULL || get_api == NULL) {
        dlclose(loader.library);
        loader.library = NULL;
        set_snapshot_state("failed", "platform provider has no compatible entry point");
        return;
    }
    status = get_api(LP3_PLATFORM_ABI_MAJOR, LP3_PLATFORM_ABI_MINOR, &api);
    if (status != LP3_PLATFORM_OK || !api_is_compatible(api)) {
        dlclose(loader.library);
        loader.library = NULL;
        set_snapshot_state("failed", "platform provider ABI is incompatible");
        return;
    }

    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_major = LP3_PLATFORM_ABI_MAJOR;
    host.abi_minor = LP3_PLATFORM_ABI_MINOR;
    host.event = provider_event;
    host.max_queued_events = LP3_MAX_QUEUED_EVENTS;
    host.max_payload_bytes = LP3_MAX_PAYLOAD_BYTES;
    if (api->probe(&host) != LP3_PLATFORM_OK ||
        api->create(&host, &instance) != LP3_PLATFORM_OK || instance == NULL ||
        api->start(instance) != LP3_PLATFORM_OK) {
        if (instance != NULL) {
            if (api->request_stop(instance) != LP3_PLATFORM_OK) {
                loader.api = api;
                loader.instance = instance;
                set_provider_snapshot_base(api);
                set_literal(loader.snapshot.state, sizeof(loader.snapshot.state), "failed");
                set_literal(loader.snapshot.error, sizeof(loader.snapshot.error),
                            "platform provider failed to stop after startup failure");
                return;
            }
            api->destroy(instance);
        }
        dlclose(loader.library);
        loader.library = NULL;
        set_snapshot_state("failed", "platform provider failed to start");
        return;
    }

    loader.api = api;
    loader.instance = instance;
    set_provider_snapshot_base(api);
    refresh_status_locked();
}

static jobjectArray snapshot_to_java(JNIEnv *env) {
    const char *values[] = {
        loader.snapshot.state,
        loader.snapshot.provider,
        loader.snapshot.build_id,
        loader.snapshot.abi_version,
        loader.snapshot.domains,
        loader.snapshot.helper_pid,
        loader.snapshot.error,
        loader.snapshot.supported_domains,
        loader.snapshot.degraded_domains,
        loader.snapshot.failed_domains,
    };
    const jsize count = (jsize)(sizeof(values) / sizeof(values[0]));
    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result;
    jsize index;

    if (string_class == NULL) {
        return NULL;
    }
    result = (*env)->NewObjectArray(env, count, string_class, NULL);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        jstring value = (*env)->NewStringUTF(env, values[index]);
        if (value == NULL) {
            return NULL;
        }
        (*env)->SetObjectArrayElement(env, result, index, value);
        (*env)->DeleteLocalRef(env, value);
    }
    return result;
}

JNIEXPORT jboolean JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_hardenProcess(
    JNIEnv *env, jclass klass) {
    (void)env;
    (void)klass;
    return harden_for_session_launcher() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_start(
    JNIEnv *env, jclass klass) {
    jobjectArray result;
    (void)klass;
    pthread_mutex_lock(&loader_lock);
    start_locked();
    result = snapshot_to_java(env);
    pthread_mutex_unlock(&loader_lock);
    return result;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_stop(
    JNIEnv *env, jclass klass) {
    jobjectArray result;
    (void)klass;
    pthread_mutex_lock(&loader_lock);
    stop_locked();
    result = snapshot_to_java(env);
    pthread_mutex_unlock(&loader_lock);
    return result;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_status(
    JNIEnv *env, jclass klass) {
    jobjectArray result;
    (void)klass;
    pthread_mutex_lock(&loader_lock);
    refresh_status_locked();
    result = snapshot_to_java(env);
    pthread_mutex_unlock(&loader_lock);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_beginEventBatch(
    JNIEnv *env, jclass klass) {
    (void)env;
    (void)klass;
    if (event_batch_held) {
        return JNI_FALSE;
    }
    pthread_mutex_lock(&event_lock);
    event_batch_held = 1;
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_endEventBatch(
    JNIEnv *env, jclass klass) {
    (void)env;
    (void)klass;
    if (!event_batch_held) {
        return JNI_FALSE;
    }
    event_batch_held = 0;
    pthread_mutex_unlock(&event_lock);
    return JNI_TRUE;
}

JNIEXPORT jlongArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_event copied[LP3_MAX_QUEUED_EVENTS + 1];
    jlong values[(LP3_MAX_QUEUED_EVENTS + 1) * 4];
    size_t count;
    size_t index;
    size_t queued_offset = 0;
    jlongArray result;
    (void)klass;

    lock_event_queues();
    if (provider_reset_pending) {
        memset(&copied[0], 0, sizeof(copied[0]));
        copied[0].type = LP3_PLATFORM_EVENT_PROVIDER_STATUS;
        provider_reset_pending = 0;
        queued_offset = 1;
    }
    count = event_count + queued_offset;
    for (index = 0; index < event_count; ++index) {
        copied[queued_offset + index] =
            event_queue[(event_head + index) % LP3_MAX_QUEUED_EVENTS];
    }
    event_head = (event_head + event_count) % LP3_MAX_QUEUED_EVENTS;
    event_count = 0;
    unlock_event_queues();

    result = (*env)->NewLongArray(env, (jsize)(count * 4));
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        values[index * 4] = (jlong)copied[index].type;
        values[index * 4 + 1] = (jlong)copied[index].unix_ms;
        values[index * 4 + 2] = (jlong)copied[index].utc_offset_seconds;
        values[index * 4 + 3] = (jlong)copied[index].is_24_hour;
    }
    if (count != 0) {
        (*env)->SetLongArrayRegion(env, result, 0, (jsize)(count * 4), values);
    }
    return result;
}

static void put_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xff);
    data[1] = (uint8_t)((value >> 8) & 0xff);
}

static void put_u32(uint8_t *data, uint32_t value) {
    unsigned int index;
    for (index = 0; index < 4; ++index) {
        data[index] = (uint8_t)((value >> (index * 8)) & 0xff);
    }
}

static void put_u64(uint8_t *data, uint64_t value) {
    unsigned int index;
    for (index = 0; index < 8; ++index) {
        data[index] = (uint8_t)((value >> (index * 8)) & 0xff);
    }
}

static void append_copied_string(uint8_t *payload, size_t *offset,
                                 const char *value) {
    size_t length = strlen(value);
    if (length != 0) {
        memcpy(payload + *offset, value, length);
        *offset += length;
    }
}

static size_t encode_notification_event(
    const struct lp3_copied_notification_event *event,
    uint8_t *payload, size_t payload_size) {
    const char *values[] = {
        event->id,
        event->replaces_id,
        event->application_id,
        event->application_name,
        event->title,
        event->body,
        event->category,
        event->icon_name,
    };
    uint32_t lengths[8];
    size_t total = 52;
    size_t offset;
    size_t index;

    for (index = 0; index < 8; ++index) {
        lengths[index] = (uint32_t)strlen(values[index]);
        total += lengths[index];
    }
    if (payload == NULL || total > payload_size) {
        return 0;
    }
    memset(payload, 0, total);
    put_u16(payload, (uint16_t)event->type);
    put_u32(payload + 4, event->flags);
    put_u64(payload + 8, (uint64_t)event->timestamp_ms);
    put_u32(payload + 16, event->close_reason);
    for (index = 0; index < 8; ++index) {
        put_u32(payload + 20 + index * 4, lengths[index]);
    }
    offset = 52;
    for (index = 0; index < 8; ++index) {
        append_copied_string(payload, &offset, values[index]);
    }
    return offset == total ? total : 0;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainNotificationEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_notification_event copied[LP3_MAX_QUEUED_EVENTS];
    uint8_t payload[52 + LP3_PLATFORM_NOTIFICATION_ID_MAX +
        LP3_PLATFORM_NOTIFICATION_ID_MAX +
        LP3_PLATFORM_NOTIFICATION_APPLICATION_ID_MAX +
        LP3_PLATFORM_NOTIFICATION_APPLICATION_NAME_MAX +
        LP3_PLATFORM_NOTIFICATION_TITLE_MAX +
        LP3_PLATFORM_NOTIFICATION_BODY_MAX +
        LP3_PLATFORM_NOTIFICATION_CATEGORY_MAX +
        LP3_PLATFORM_NOTIFICATION_ICON_NAME_MAX];
    jclass byte_array_class;
    jobjectArray result;
    size_t count;
    size_t index;
    (void)klass;

    lock_event_queues();
    /* A reset marker must cross JNI before any replacement-helper event. */
    count = provider_reset_pending ? 0 : notification_event_count;
    for (index = 0; index < count; ++index) {
        copied[index] = notification_event_queue[
            (notification_event_head + index) % LP3_MAX_QUEUED_EVENTS];
    }
    notification_event_head =
        (notification_event_head + count) % LP3_MAX_QUEUED_EVENTS;
    notification_event_count -= count;
    unlock_event_queues();

    byte_array_class = (*env)->FindClass(env, "[B");
    if (byte_array_class == NULL) {
        return NULL;
    }
    result = (*env)->NewObjectArray(env, (jsize)count, byte_array_class, NULL);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        const size_t encoded = encode_notification_event(
            &copied[index], payload, sizeof(payload));
        jbyteArray record;
        if (encoded == 0 || encoded > INT_MAX) {
            return NULL;
        }
        record = (*env)->NewByteArray(env, (jsize)encoded);
        if (record == NULL) {
            return NULL;
        }
        (*env)->SetByteArrayRegion(env, record, 0, (jsize)encoded,
                                  (const jbyte *)payload);
        (*env)->SetObjectArrayElement(env, result, (jsize)index, record);
        (*env)->DeleteLocalRef(env, record);
    }
    return result;
}

static size_t encode_call_event(
    const struct lp3_copied_call_event *event,
    uint8_t *payload, size_t payload_size) {
    const char *values[] = { event->id, event->name, event->number };
    uint32_t lengths[3];
    size_t total = 20;
    size_t offset;
    size_t index;

    for (index = 0; index < 3; ++index) {
        lengths[index] = (uint32_t)strlen(values[index]);
        total += lengths[index];
    }
    if (payload == NULL || total > payload_size) {
        return 0;
    }
    memset(payload, 0, total);
    put_u16(payload, LP3_PLATFORM_EVENT_CALL);
    put_u32(payload + 4, event->state);
    for (index = 0; index < 3; ++index) {
        put_u32(payload + 8 + index * 4, lengths[index]);
    }
    offset = 20;
    for (index = 0; index < 3; ++index) {
        append_copied_string(payload, &offset, values[index]);
    }
    return offset == total ? total : 0;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainCallEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_call_event copied[LP3_MAX_QUEUED_EVENTS];
    uint8_t payload[20 + LP3_PLATFORM_CALL_ID_MAX +
        LP3_PLATFORM_CALL_NAME_MAX + LP3_PLATFORM_CALL_NUMBER_MAX];
    jclass byte_array_class;
    jobjectArray result;
    size_t count;
    size_t index;
    (void)klass;

    lock_event_queues();
    /* A reset marker must cross JNI before any replacement-helper event. */
    count = provider_reset_pending ? 0 : call_event_count;
    for (index = 0; index < count; ++index) {
        copied[index] = call_event_queue[
            (call_event_head + index) % LP3_MAX_QUEUED_EVENTS];
    }
    call_event_head = (call_event_head + count) % LP3_MAX_QUEUED_EVENTS;
    call_event_count -= count;
    unlock_event_queues();

    byte_array_class = (*env)->FindClass(env, "[B");
    if (byte_array_class == NULL) {
        return NULL;
    }
    result = (*env)->NewObjectArray(env, (jsize)count, byte_array_class, NULL);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        const size_t encoded = encode_call_event(
            &copied[index], payload, sizeof(payload));
        jbyteArray record;
        if (encoded == 0 || encoded > INT_MAX) {
            return NULL;
        }
        record = (*env)->NewByteArray(env, (jsize)encoded);
        if (record == NULL) {
            return NULL;
        }
        (*env)->SetByteArrayRegion(env, record, 0, (jsize)encoded,
                                  (const jbyte *)payload);
        (*env)->SetObjectArrayElement(env, result, (jsize)index, record);
        (*env)->DeleteLocalRef(env, record);
    }
    return result;
}

JNIEXPORT jlongArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainMediaEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_media_event copied[LP3_MAX_QUEUED_EVENTS];
    jlong values[LP3_MAX_QUEUED_EVENTS * 2];
    jlongArray result;
    size_t count;
    size_t index;
    (void)klass;

    lock_event_queues();
    /* A reset marker must cross JNI before any replacement-helper event. */
    count = provider_reset_pending ? 0 : media_event_count;
    for (index = 0; index < count; ++index) {
        copied[index] = media_event_queue[
            (media_event_head + index) % LP3_MAX_QUEUED_EVENTS];
    }
    media_event_head = (media_event_head + count) % LP3_MAX_QUEUED_EVENTS;
    media_event_count -= count;
    unlock_event_queues();

    result = (*env)->NewLongArray(env, (jsize)(count * 2));
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        values[index * 2] = (jlong)copied[index].flags;
        values[index * 2 + 1] = (jlong)copied[index].volume_percent;
    }
    if (count != 0) {
        (*env)->SetLongArrayRegion(env, result, 0, (jsize)(count * 2), values);
    }
    return result;
}

JNIEXPORT jlongArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
    JNIEnv *env, jclass klass, jint accuracy_value, jint timeout_ms_value) {
    struct lp3_platform_location_request_v1 request;
    jlong values[2];
    jlongArray result;
    uint64_t request_id = 0;
    int32_t status = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held = 0;
    (void)klass;

    if ((accuracy_value != LP3_PLATFORM_LOCATION_COARSE &&
         accuracy_value != LP3_PLATFORM_LOCATION_FINE) || timeout_ms_value <= 0 ||
        (uint32_t)timeout_ms_value > LP3_LOCATION_TIMEOUT_MAX_MS) {
        status = LP3_PLATFORM_INVALID_ARGUMENT;
        goto done;
    }
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(LP3_PLATFORM_DOMAIN_LOCATION);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_LOCATION) != 0 &&
        API_HAS_MEMBER(loader.api, location_query) && loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        lock_event_queues();
        if (location_outstanding_count < LP3_MAX_QUEUED_EVENTS) {
            request_id = loader.next_request_id++;
            location_outstanding[location_outstanding_count++] = request_id;
            unlock_event_queues();
            memset(&request, 0, sizeof(request));
            request.struct_size = sizeof(request);
            request.accuracy = (uint32_t)accuracy_value;
            request.timeout_ms = (uint32_t)timeout_ms_value;
            status = loader.api->location_query(loader.instance, request_id, &request);
            if (status != LP3_PLATFORM_OK) {
                lock_event_queues();
                {
                    int index = location_id_index(location_outstanding,
                                                   location_outstanding_count,
                                                   request_id);
                    if (index >= 0) {
                        location_remove_id(location_outstanding,
                                           &location_outstanding_count,
                                           (size_t)index);
                    }
                }
                unlock_event_queues();
                request_id = 0;
            }
        } else {
            unlock_event_queues();
            status = LP3_PLATFORM_BUSY;
        }
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);

done:
    values[0] = (jlong)status;
    values[1] = (jlong)request_id;
    result = (*env)->NewLongArray(env, 2);
    if (result != NULL) {
        (*env)->SetLongArrayRegion(env, result, 0, 2, values);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_cancelLocation(
    JNIEnv *env, jclass klass, jlong request_id_value) {
    uint64_t request_id;
    int32_t status = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)env;
    (void)klass;

    if (request_id_value <= 0 ||
        ((uint64_t)request_id_value & (UINT64_C(1) << 63)) != 0) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    request_id = (uint64_t)request_id_value;
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(LP3_PLATFORM_DOMAIN_LOCATION);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_LOCATION) != 0) {
        int index;
        lock_event_queues();
        index = location_id_index(location_outstanding, location_outstanding_count,
                                  request_id);
        if (index < 0) {
            status = LP3_PLATFORM_INVALID_ARGUMENT;
        } else {
            location_remove_id(location_outstanding, &location_outstanding_count,
                               (size_t)index);
            if (location_tombstone_count >= LP3_MAX_QUEUED_EVENTS) {
                /* IDs are monotonic and never reused; an evicted late reply is
                 * still ignored as unknown before its payload is inspected. */
                location_remove_id(location_tombstones,
                                   &location_tombstone_count, 0);
            }
            location_tombstones[location_tombstone_count++] = request_id;
            status = LP3_PLATFORM_OK;
        }
        unlock_event_queues();
        if (status == LP3_PLATFORM_OK) {
            status = API_HAS_MEMBER(loader.api, cancel) ?
                loader.api->cancel(loader.instance, request_id) :
                LP3_PLATFORM_NOT_SUPPORTED;
        }
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);
    return status;
}

JNIEXPORT jlongArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainLocationEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_location_event copied[LP3_MAX_QUEUED_EVENTS];
    jlong values[LP3_MAX_QUEUED_EVENTS * 6];
    jlongArray result;
    size_t count;
    size_t index;
    (void)klass;

    lock_event_queues();
    /* A reset marker must cross JNI before any replacement-helper completion. */
    count = provider_reset_pending ? 0 : location_event_count;
    for (index = 0; index < count; ++index) {
        copied[index] = location_event_queue[
            (location_event_head + index) % LP3_MAX_QUEUED_EVENTS];
    }
    location_event_head = (location_event_head + count) % LP3_MAX_QUEUED_EVENTS;
    location_event_count -= count;
    unlock_event_queues();

    result = (*env)->NewLongArray(env, (jsize)(count * 6));
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        values[index * 6] = (jlong)copied[index].request_id;
        values[index * 6 + 1] = (jlong)copied[index].status;
        values[index * 6 + 2] = (jlong)copied[index].latitude_e7;
        values[index * 6 + 3] = (jlong)copied[index].longitude_e7;
        values[index * 6 + 4] = (jlong)copied[index].accuracy_m;
        values[index * 6 + 5] = (jlong)copied[index].timestamp_ms;
    }
    if (count != 0) {
        (*env)->SetLongArrayRegion(env, result, 0, (jsize)(count * 6), values);
    }
    return result;
}

JNIEXPORT jlongArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_calendarStart(
    JNIEnv *env, jclass klass, jint kind_value, jint max_records_value,
    jint offset_value, jlong start_ms_value, jlong end_ms_value,
    jstring calendar_id_value) {
    struct lp3_platform_calendar_query_v1 request;
    char calendar_id[LP3_PLATFORM_CALENDAR_ID_MAX + 1];
    uint32_t calendar_id_size = 0;
    jlong values[2];
    jlongArray result;
    uint64_t request_id = 0;
    int32_t status = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held = 0;
    (void)klass;

    if ((kind_value != LP3_PLATFORM_CALENDAR_QUERY_CALENDARS &&
         kind_value != LP3_PLATFORM_CALENDAR_QUERY_EVENTS) ||
        max_records_value <= 0 ||
        max_records_value > (jint)LP3_PLATFORM_CALENDAR_PAGE_MAX ||
        offset_value < 0 || offset_value > 512 ||
        !java_string_to_utf8(env, calendar_id_value, calendar_id,
                            LP3_PLATFORM_CALENDAR_ID_MAX, &calendar_id_size, 1) ||
        (kind_value == LP3_PLATFORM_CALENDAR_QUERY_CALENDARS &&
         (start_ms_value != 0 || end_ms_value != 0 ||
          calendar_id_size != 0)) ||
        (kind_value == LP3_PLATFORM_CALENDAR_QUERY_EVENTS &&
         (calendar_id_size == 0 || start_ms_value >= end_ms_value ||
          (uint64_t)end_ms_value - (uint64_t)start_ms_value >
              UINT64_C(370) * 24 * 60 * 60 * 1000))) {
        status = LP3_PLATFORM_INVALID_ARGUMENT;
        goto done;
    }
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_CALENDAR);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_CALENDAR) != 0 &&
        API_HAS_MEMBER(loader.api, calendar_query) &&
        loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        lock_event_queues();
        if (calendar_outstanding_count < LP3_MAX_QUEUED_EVENTS) {
            request_id = loader.next_request_id++;
            calendar_outstanding[calendar_outstanding_count++] = request_id;
            unlock_event_queues();
            memset(&request, 0, sizeof(request));
            request.struct_size = sizeof(request);
            request.kind = (uint32_t)kind_value;
            request.max_records = (uint32_t)max_records_value;
            request.offset = (uint32_t)offset_value;
            request.start_ms = (int64_t)start_ms_value;
            request.end_ms = (int64_t)end_ms_value;
            request.calendar_id.data = calendar_id_size == 0 ? NULL : calendar_id;
            request.calendar_id.size = calendar_id_size;
            status = loader.api->calendar_query(
                loader.instance, request_id, &request);
            if (status != LP3_PLATFORM_OK) {
                int index;
                lock_event_queues();
                index = location_id_index(calendar_outstanding,
                                           calendar_outstanding_count,
                                           request_id);
                if (index >= 0) {
                    location_remove_id(calendar_outstanding,
                                       &calendar_outstanding_count,
                                       (size_t)index);
                }
                unlock_event_queues();
                request_id = 0;
            }
        } else {
            unlock_event_queues();
            status = LP3_PLATFORM_BUSY;
        }
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);

done:
    values[0] = (jlong)status;
    values[1] = (jlong)request_id;
    result = (*env)->NewLongArray(env, 2);
    if (result != NULL) {
        (*env)->SetLongArrayRegion(env, result, 0, 2, values);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_cancelCalendar(
    JNIEnv *env, jclass klass, jlong request_id_value) {
    uint64_t request_id;
    int32_t status = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)env;
    (void)klass;

    if (request_id_value <= 0 ||
        ((uint64_t)request_id_value & (UINT64_C(1) << 63)) != 0) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    request_id = (uint64_t)request_id_value;
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_CALENDAR);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_CALENDAR) != 0) {
        int index;
        lock_event_queues();
        index = location_id_index(calendar_outstanding,
                                  calendar_outstanding_count, request_id);
        if (index < 0) {
            status = LP3_PLATFORM_INVALID_ARGUMENT;
        } else {
            location_remove_id(calendar_outstanding,
                               &calendar_outstanding_count, (size_t)index);
            if (calendar_tombstone_count >= LP3_MAX_QUEUED_EVENTS) {
                location_remove_id(calendar_tombstones,
                                   &calendar_tombstone_count, 0);
            }
            calendar_tombstones[calendar_tombstone_count++] = request_id;
            status = LP3_PLATFORM_OK;
        }
        unlock_event_queues();
        if (status == LP3_PLATFORM_OK) {
            status = API_HAS_MEMBER(loader.api, cancel) ?
                loader.api->cancel(loader.instance, request_id) :
                LP3_PLATFORM_NOT_SUPPORTED;
        }
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);
    return status;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainCalendarEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_calendar_event copied[LP3_MAX_QUEUED_EVENTS];
    jclass byte_array_class;
    jobjectArray result;
    size_t count;
    size_t index;
    (void)klass;

    memset(copied, 0, sizeof(copied));
    lock_event_queues();
    count = provider_reset_pending ? 0 : calendar_event_count;
    for (index = 0; index < count; ++index) {
        size_t slot = (calendar_event_head + index) % LP3_MAX_QUEUED_EVENTS;
        copied[index] = calendar_event_queue[slot];
        memset(&calendar_event_queue[slot], 0,
               sizeof(calendar_event_queue[slot]));
    }
    calendar_event_head = (calendar_event_head + count) %
        LP3_MAX_QUEUED_EVENTS;
    calendar_event_count -= count;
    unlock_event_queues();

    byte_array_class = (*env)->FindClass(env, "[B");
    if (byte_array_class == NULL) {
        for (index = 0; index < count; ++index) free(copied[index].payload);
        return NULL;
    }
    result = (*env)->NewObjectArray(env, (jsize)count, byte_array_class, NULL);
    if (result == NULL) {
        for (index = 0; index < count; ++index) free(copied[index].payload);
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        jbyteArray record = (*env)->NewByteArray(env, (jsize)copied[index].size);
        if (record == NULL) {
            size_t remaining;
            for (remaining = index; remaining < count; ++remaining) {
                free(copied[remaining].payload);
            }
            return NULL;
        }
        (*env)->SetByteArrayRegion(env, record, 0,
                                  (jsize)copied[index].size,
                                  (const jbyte *)copied[index].payload);
        (*env)->SetObjectArrayElement(env, result, (jsize)index, record);
        (*env)->DeleteLocalRef(env, record);
        free(copied[index].payload);
    }
    return result;
}

JNIEXPORT jlongArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_contactStart(
    JNIEnv *env, jclass klass, jint kind_value, jint max_records_value,
    jint offset_value, jstring query_value) {
    struct lp3_platform_contact_query_v1 request;
    char query[LP3_PLATFORM_CONTACT_NUMBER_MAX + 1];
    uint32_t query_size = 0;
    jlong values[2];
    jlongArray result;
    uint64_t request_id = 0;
    int32_t status = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held = 0;
    (void)klass;

    if ((kind_value != LP3_PLATFORM_CONTACT_QUERY_LIST &&
         kind_value != LP3_PLATFORM_CONTACT_QUERY_PHONE) ||
        max_records_value <= 0 ||
        max_records_value > (jint)LP3_PLATFORM_CONTACT_PAGE_MAX ||
        offset_value < 0 || offset_value > 4096 ||
        !java_string_to_utf8(env, query_value, query,
                            LP3_PLATFORM_CONTACT_NUMBER_MAX, &query_size, 1) ||
        (kind_value == LP3_PLATFORM_CONTACT_QUERY_LIST && query_size != 0) ||
        (kind_value == LP3_PLATFORM_CONTACT_QUERY_PHONE &&
         (max_records_value != 1 || offset_value != 0 || query_size == 0))) {
        status = LP3_PLATFORM_INVALID_ARGUMENT;
        goto done;
    }
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_CONTACTS);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_CONTACTS) != 0 &&
        API_HAS_MEMBER(loader.api, contact_query) && loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        lock_event_queues();
        if (contact_outstanding_count < LP3_MAX_QUEUED_EVENTS) {
            request_id = loader.next_request_id++;
            contact_outstanding[contact_outstanding_count++] = request_id;
            unlock_event_queues();
            memset(&request, 0, sizeof(request));
            request.struct_size = sizeof(request);
            request.kind = (uint32_t)kind_value;
            request.max_records = (uint32_t)max_records_value;
            request.offset = (uint32_t)offset_value;
            request.query.data = query_size == 0 ? NULL : query;
            request.query.size = query_size;
            status = loader.api->contact_query(
                loader.instance, request_id, &request);
            if (status != LP3_PLATFORM_OK) {
                int index;
                lock_event_queues();
                index = location_id_index(contact_outstanding,
                                          contact_outstanding_count,
                                          request_id);
                if (index >= 0) {
                    location_remove_id(contact_outstanding,
                                       &contact_outstanding_count,
                                       (size_t)index);
                }
                unlock_event_queues();
                request_id = 0;
            }
        } else {
            unlock_event_queues();
            status = LP3_PLATFORM_BUSY;
        }
    }
    if (command_gate_held) end_provider_command_dispatch();
    pthread_mutex_unlock(&loader_lock);

done:
    values[0] = (jlong)status;
    values[1] = (jlong)request_id;
    result = (*env)->NewLongArray(env, 2);
    if (result != NULL) {
        (*env)->SetLongArrayRegion(env, result, 0, 2, values);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_cancelContact(
    JNIEnv *env, jclass klass, jlong request_id_value) {
    uint64_t request_id;
    int32_t status = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)env;
    (void)klass;

    if (request_id_value <= 0 ||
        ((uint64_t)request_id_value & (UINT64_C(1) << 63)) != 0) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    request_id = (uint64_t)request_id_value;
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_CONTACTS);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_CONTACTS) != 0) {
        int index;
        lock_event_queues();
        index = location_id_index(contact_outstanding,
                                  contact_outstanding_count, request_id);
        if (index < 0) {
            status = LP3_PLATFORM_INVALID_ARGUMENT;
        } else {
            location_remove_id(contact_outstanding,
                               &contact_outstanding_count, (size_t)index);
            if (contact_tombstone_count >= LP3_MAX_QUEUED_EVENTS) {
                location_remove_id(contact_tombstones,
                                   &contact_tombstone_count, 0);
            }
            contact_tombstones[contact_tombstone_count++] = request_id;
            status = LP3_PLATFORM_OK;
        }
        unlock_event_queues();
        if (status == LP3_PLATFORM_OK) {
            status = API_HAS_MEMBER(loader.api, cancel) ?
                loader.api->cancel(loader.instance, request_id) :
                LP3_PLATFORM_NOT_SUPPORTED;
        }
    }
    if (command_gate_held) end_provider_command_dispatch();
    pthread_mutex_unlock(&loader_lock);
    return status;
}

JNIEXPORT jobjectArray JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainContactEvents(
    JNIEnv *env, jclass klass) {
    struct lp3_copied_contact_event copied[LP3_MAX_QUEUED_EVENTS];
    jclass byte_array_class;
    jobjectArray result;
    size_t count;
    size_t index;
    (void)klass;

    memset(copied, 0, sizeof(copied));
    lock_event_queues();
    count = provider_reset_pending ? 0 : contact_event_count;
    for (index = 0; index < count; ++index) {
        size_t slot = (contact_event_head + index) % LP3_MAX_QUEUED_EVENTS;
        copied[index] = contact_event_queue[slot];
        memset(&contact_event_queue[slot], 0,
               sizeof(contact_event_queue[slot]));
    }
    contact_event_head = (contact_event_head + count) % LP3_MAX_QUEUED_EVENTS;
    contact_event_count -= count;
    unlock_event_queues();

    byte_array_class = (*env)->FindClass(env, "[B");
    if (byte_array_class == NULL) {
        for (index = 0; index < count; ++index) free(copied[index].payload);
        return NULL;
    }
    result = (*env)->NewObjectArray(env, (jsize)count, byte_array_class, NULL);
    if (result == NULL) {
        for (index = 0; index < count; ++index) free(copied[index].payload);
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        jbyteArray record = (*env)->NewByteArray(env, (jsize)copied[index].size);
        if (record == NULL) {
            size_t remaining;
            for (remaining = index; remaining < count; ++remaining) {
                free(copied[remaining].payload);
            }
            return NULL;
        }
        (*env)->SetByteArrayRegion(env, record, 0,
                                  (jsize)copied[index].size,
                                  (const jbyte *)copied[index].payload);
        (*env)->SetObjectArrayElement(env, result, (jsize)index, record);
        (*env)->DeleteLocalRef(env, record);
        free(copied[index].payload);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
    JNIEnv *env, jclass klass, jint command_value, jstring id_value) {
    const char *id;
    jsize id_size;
    size_t index;
    struct lp3_platform_notification_command_v1 command;
    uint64_t request_id;
    uint64_t command_domains;
    int32_t result = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)klass;

    if (id_value == NULL ||
        (command_value != LP3_PLATFORM_NOTIFICATION_DISMISS &&
         command_value != LP3_PLATFORM_NOTIFICATION_OPEN)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    id_size = (*env)->GetStringUTFLength(env, id_value);
    id = (*env)->GetStringUTFChars(env, id_value, NULL);
    if (id == NULL) {
        return LP3_PLATFORM_INTERNAL_ERROR;
    }
    if (id_size <= 0 ||
        (uint32_t)id_size > LP3_PLATFORM_NOTIFICATION_ID_MAX) {
        result = LP3_PLATFORM_INVALID_ARGUMENT;
        goto done;
    }
    for (index = 0; index < (size_t)id_size; ++index) {
        if (id[index] < '0' || id[index] > '9') {
            result = LP3_PLATFORM_INVALID_ARGUMENT;
            goto done;
        }
    }

    command_domains = LP3_PLATFORM_DOMAIN_NOTIFICATIONS;
    if (command_value == LP3_PLATFORM_NOTIFICATION_OPEN) {
        command_domains |= LP3_PLATFORM_DOMAIN_MESSAGING;
    }
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        command_domains);
    if (command_gate_held &&
        loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & command_domains) == command_domains &&
        API_HAS_MEMBER(loader.api, notification_command) &&
        loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        request_id = loader.next_request_id++;
        memset(&command, 0, sizeof(command));
        command.struct_size = sizeof(command);
        command.command = (uint32_t)command_value;
        command.id.data = id;
        command.id.size = (uint32_t)id_size;
        result = loader.api->notification_command(
            loader.instance, request_id, &command);
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);

done:
    (*env)->ReleaseStringUTFChars(env, id_value, id);
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
    JNIEnv *env, jclass klass, jstring id_value, jstring text_value) {
    const char *id;
    jsize id_size;
    size_t index;
    char text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 1];
    uint32_t text_size = 0;
    struct lp3_platform_message_v1 message;
    uint64_t request_id;
    int32_t result = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)klass;

    if (id_value == NULL || text_value == NULL) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    id_size = (*env)->GetStringUTFLength(env, id_value);
    id = (*env)->GetStringUTFChars(env, id_value, NULL);
    if (id == NULL) {
        return LP3_PLATFORM_INTERNAL_ERROR;
    }
    if (id_size <= 0 ||
        (uint32_t)id_size > LP3_PLATFORM_MESSAGE_CONVERSATION_ID_MAX) {
        result = LP3_PLATFORM_INVALID_ARGUMENT;
        goto done_reply;
    }
    for (index = 0; index < (size_t)id_size; ++index) {
        if (id[index] < '0' || id[index] > '9') {
            result = LP3_PLATFORM_INVALID_ARGUMENT;
            goto done_reply;
        }
    }
    if (!java_string_to_utf8(env, text_value, text,
                             LP3_PLATFORM_MESSAGE_TEXT_MAX,
                             &text_size, 0)) {
        result = (*env)->ExceptionCheck(env) ?
            LP3_PLATFORM_INTERNAL_ERROR : LP3_PLATFORM_INVALID_ARGUMENT;
        goto done_reply;
    }

    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_NOTIFICATIONS | LP3_PLATFORM_DOMAIN_MESSAGING);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains &
         (LP3_PLATFORM_DOMAIN_NOTIFICATIONS | LP3_PLATFORM_DOMAIN_MESSAGING)) ==
            (LP3_PLATFORM_DOMAIN_NOTIFICATIONS | LP3_PLATFORM_DOMAIN_MESSAGING) &&
        API_HAS_MEMBER(loader.api, reply_message) &&
        loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        request_id = loader.next_request_id++;
        memset(&message, 0, sizeof(message));
        message.struct_size = sizeof(message);
        message.conversation_id.data = id;
        message.conversation_id.size = (uint32_t)id_size;
        message.text.data = text;
        message.text.size = text_size;
        result = loader.api->reply_message(
            loader.instance, request_id, &message);
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);

done_reply:
    (*env)->ReleaseStringUTFChars(env, id_value, id);
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_sendMessage(
    JNIEnv *env, jclass klass, jstring account_value,
    jstring recipient_value, jstring text_value) {
    static const char account_prefix[] =
        "/org/freedesktop/Telepathy/Account/";
    char account[LP3_PLATFORM_MESSAGE_ACCOUNT_ID_MAX + 1];
    char recipient[LP3_PLATFORM_MESSAGE_RECIPIENT_MAX + 1];
    char text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 1];
    uint32_t account_size = 0;
    uint32_t recipient_size = 0;
    uint32_t text_size = 0;
    struct lp3_platform_outgoing_message_v1 message;
    uint64_t request_id;
    int32_t result = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)klass;

    if (!java_string_to_utf8(env, account_value, account,
                             LP3_PLATFORM_MESSAGE_ACCOUNT_ID_MAX,
                             &account_size, 0) ||
        !java_string_to_utf8(env, recipient_value, recipient,
                             LP3_PLATFORM_MESSAGE_RECIPIENT_MAX,
                             &recipient_size, 0) ||
        !java_string_to_utf8(env, text_value, text,
                             LP3_PLATFORM_MESSAGE_TEXT_MAX,
                             &text_size, 0) ||
        account_size <= sizeof(account_prefix) - 1 ||
        memcmp(account, account_prefix, sizeof(account_prefix) - 1) != 0) {
        return (*env)->ExceptionCheck(env) ?
            LP3_PLATFORM_INTERNAL_ERROR : LP3_PLATFORM_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_MESSAGING);
    if (command_gate_held && loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_MESSAGING) != 0 &&
        API_HAS_MEMBER(loader.api, send_message) &&
        loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        request_id = loader.next_request_id++;
        memset(&message, 0, sizeof(message));
        message.struct_size = sizeof(message);
        message.account_id.data = account;
        message.account_id.size = account_size;
        message.recipient.data = recipient;
        message.recipient.size = recipient_size;
        message.text.data = text;
        message.text.size = text_size;
        result = loader.api->send_message(loader.instance, request_id, &message);
    }
    if (command_gate_held) end_provider_command_dispatch();
    pthread_mutex_unlock(&loader_lock);
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_callCommand(
    JNIEnv *env, jclass klass, jint command_value, jstring id_value) {
    const char *id;
    jsize id_size;
    size_t index;
    struct lp3_platform_call_command_v1 command;
    uint64_t request_id;
    int32_t result = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)klass;

    if (id_value == NULL ||
        (command_value != LP3_PLATFORM_CALL_ANSWER &&
         command_value != LP3_PLATFORM_CALL_HANG_UP &&
         command_value != LP3_PLATFORM_CALL_SILENCE)) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    id_size = (*env)->GetStringUTFLength(env, id_value);
    id = (*env)->GetStringUTFChars(env, id_value, NULL);
    if (id == NULL) {
        return LP3_PLATFORM_INTERNAL_ERROR;
    }
    if (id_size < 0 || (uint32_t)id_size > LP3_PLATFORM_CALL_ID_MAX ||
        (command_value == LP3_PLATFORM_CALL_SILENCE ? id_size != 0 :
                                                     id_size == 0)) {
        result = LP3_PLATFORM_INVALID_ARGUMENT;
        goto done;
    }
    for (index = 0; index < (size_t)id_size; ++index) {
        const unsigned char character = (unsigned char)id[index];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            result = LP3_PLATFORM_INVALID_ARGUMENT;
            goto done;
        }
    }

    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_CALLS);
    if (command_gate_held &&
        loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_CALLS) != 0 &&
        API_HAS_MEMBER(loader.api, call_command) &&
        loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        request_id = loader.next_request_id++;
        memset(&command, 0, sizeof(command));
        command.struct_size = sizeof(command);
        command.command = (uint32_t)command_value;
        command.call_id.data = id_size == 0 ? NULL : id;
        command.call_id.size = (uint32_t)id_size;
        result = loader.api->call_command(
            loader.instance, request_id, &command);
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);

done:
    (*env)->ReleaseStringUTFChars(env, id_value, id);
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_mediaCommand(
    JNIEnv *env, jclass klass, jint command_value) {
    uint64_t request_id;
    int32_t result = LP3_PLATFORM_UNAVAILABLE;
    int command_gate_held;
    (void)env;
    (void)klass;

    if (command_value != LP3_PLATFORM_MEDIA_VOLUME_UP &&
        command_value != LP3_PLATFORM_MEDIA_VOLUME_DOWN) {
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&loader_lock);
    command_gate_held = begin_provider_command_dispatch(
        LP3_PLATFORM_DOMAIN_MEDIA);
    if (command_gate_held &&
        loader.api != NULL && loader.instance != NULL &&
        (loader.api->info.domains & LP3_PLATFORM_DOMAIN_MEDIA) != 0 &&
        API_HAS_MEMBER(loader.api, media_command) &&
        loader.next_request_id != 0 &&
        (loader.next_request_id & (UINT64_C(1) << 63)) == 0) {
        request_id = loader.next_request_id++;
        result = loader.api->media_command(
            loader.instance, request_id, (uint32_t)command_value);
    }
    if (command_gate_held) {
        end_provider_command_dispatch();
    }
    pthread_mutex_unlock(&loader_lock);
    return result;
}
