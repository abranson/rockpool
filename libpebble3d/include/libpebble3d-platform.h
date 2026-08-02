/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * libpebble3d Sailfish platform-provider ABI, version 1.
 *
 * All ABI records begin with struct_size and are append-only.  The caller
 * initializes an output record's struct_size to the space it provides; a
 * provider writes only fields that fit.  Pointer-plus-length views are
 * borrowed only for the duration of the call or callback.  Providers must
 * copy any view they retain.  Request IDs are allocated by the daemon and
 * are never reused while a provider instance exists.
 */

#ifndef LIBPEBBLE3D_PLATFORM_H
#define LIBPEBBLE3D_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LP3_PLATFORM_ABI_MAJOR 1u
#define LP3_PLATFORM_ABI_MINOR 3u

#define LP3_PLATFORM_NOTIFICATION_ID_MAX 64u
#define LP3_PLATFORM_NOTIFICATION_APPLICATION_ID_MAX 256u
#define LP3_PLATFORM_NOTIFICATION_APPLICATION_NAME_MAX 256u
#define LP3_PLATFORM_NOTIFICATION_TITLE_MAX 512u
#define LP3_PLATFORM_NOTIFICATION_BODY_MAX 4096u
#define LP3_PLATFORM_NOTIFICATION_CATEGORY_MAX 128u
#define LP3_PLATFORM_NOTIFICATION_ICON_NAME_MAX 128u
#define LP3_PLATFORM_CALL_ID_MAX 128u
#define LP3_PLATFORM_CALL_NAME_MAX 256u
#define LP3_PLATFORM_CALL_NUMBER_MAX 256u

enum lp3_platform_status {
    LP3_PLATFORM_OK = 0,
    LP3_PLATFORM_CANCELLED = 1,
    LP3_PLATFORM_INVALID_ARGUMENT = 2,
    LP3_PLATFORM_NOT_SUPPORTED = 3,
    LP3_PLATFORM_BUSY = 4,
    LP3_PLATFORM_UNAVAILABLE = 5,
    LP3_PLATFORM_IO_ERROR = 6,
    LP3_PLATFORM_PROTOCOL_ERROR = 7,
    LP3_PLATFORM_INTERNAL_ERROR = 8
};

enum lp3_platform_domain {
    LP3_PLATFORM_DOMAIN_NOTIFICATIONS = 1u << 0,
    LP3_PLATFORM_DOMAIN_MESSAGING = 1u << 1,
    LP3_PLATFORM_DOMAIN_MEDIA = 1u << 2,
    LP3_PLATFORM_DOMAIN_CALLS = 1u << 3,
    LP3_PLATFORM_DOMAIN_CALENDAR = 1u << 4,
    LP3_PLATFORM_DOMAIN_CONTACTS = 1u << 5,
    LP3_PLATFORM_DOMAIN_LOCATION = 1u << 6,
    LP3_PLATFORM_DOMAIN_TIME = 1u << 7,
    LP3_PLATFORM_DOMAIN_DEVICE_STATE = 1u << 8,
    LP3_PLATFORM_DOMAIN_PROFILES = 1u << 9
};

#define LP3_PLATFORM_DOMAIN_ALL ((uint64_t)(LP3_PLATFORM_DOMAIN_NOTIFICATIONS | \
    LP3_PLATFORM_DOMAIN_MESSAGING | LP3_PLATFORM_DOMAIN_MEDIA | \
    LP3_PLATFORM_DOMAIN_CALLS | LP3_PLATFORM_DOMAIN_CALENDAR | \
    LP3_PLATFORM_DOMAIN_CONTACTS | LP3_PLATFORM_DOMAIN_LOCATION | \
    LP3_PLATFORM_DOMAIN_TIME | LP3_PLATFORM_DOMAIN_DEVICE_STATE | \
    LP3_PLATFORM_DOMAIN_PROFILES))

enum lp3_platform_event_type {
    LP3_PLATFORM_EVENT_REQUEST_COMPLETE = 1,
    LP3_PLATFORM_EVENT_NOTIFICATION = 2,
    LP3_PLATFORM_EVENT_NOTIFICATION_CLOSED = 3,
    LP3_PLATFORM_EVENT_MEDIA = 4,
    LP3_PLATFORM_EVENT_CALL = 5,
    LP3_PLATFORM_EVENT_CALENDAR = 6,
    LP3_PLATFORM_EVENT_CONTACT = 7,
    LP3_PLATFORM_EVENT_LOCATION = 8,
    LP3_PLATFORM_EVENT_TIME_CHANGED = 9,
    LP3_PLATFORM_EVENT_DEVICE_STATE = 10,
    LP3_PLATFORM_EVENT_PROFILE = 11,
    LP3_PLATFORM_EVENT_PROVIDER_STATUS = 12
};

enum lp3_platform_notification_flag {
    LP3_PLATFORM_NOTIFICATION_HAS_DEFAULT_ACTION = 1u << 0
};

enum lp3_platform_notification_command {
    LP3_PLATFORM_NOTIFICATION_DISMISS = 1,
    LP3_PLATFORM_NOTIFICATION_OPEN = 2
};

enum lp3_platform_media_command {
    LP3_PLATFORM_MEDIA_PLAY_PAUSE = 1,
    LP3_PLATFORM_MEDIA_NEXT = 2,
    LP3_PLATFORM_MEDIA_PREVIOUS = 3,
    LP3_PLATFORM_MEDIA_VOLUME_UP = 4,
    LP3_PLATFORM_MEDIA_VOLUME_DOWN = 5
};

enum lp3_platform_media_flag {
    /* The event carries the current Sailfish system output volume. */
    LP3_PLATFORM_MEDIA_SYSTEM_VOLUME = 1u << 0
};

enum lp3_platform_call_command {
    LP3_PLATFORM_CALL_ANSWER = 1,
    LP3_PLATFORM_CALL_HANG_UP = 2,
    LP3_PLATFORM_CALL_SILENCE = 3
};

enum lp3_platform_call_state {
    LP3_PLATFORM_CALL_ENDED = 0,
    LP3_PLATFORM_CALL_RINGING = 1,
    LP3_PLATFORM_CALL_DIALING = 2,
    LP3_PLATFORM_CALL_ACTIVE = 3,
    LP3_PLATFORM_CALL_HELD = 4
};

enum lp3_platform_location_accuracy {
    LP3_PLATFORM_LOCATION_COARSE = 1,
    LP3_PLATFORM_LOCATION_FINE = 2
};

enum lp3_platform_provider_state {
    LP3_PLATFORM_PROVIDER_READY = 1,
    LP3_PLATFORM_PROVIDER_DEGRADED = 2,
    LP3_PLATFORM_PROVIDER_FAILED = 3
};

struct lp3_platform_bytes {
    const uint8_t *data;
    uint32_t size;
};

struct lp3_platform_string {
    const char *data;
    uint32_t size;
};

struct lp3_platform_notification_v1 {
    uint32_t struct_size;
    uint32_t flags;
    struct lp3_platform_string id;
    struct lp3_platform_string application_id;
    struct lp3_platform_string application_name;
    struct lp3_platform_string title;
    struct lp3_platform_string body;
    struct lp3_platform_bytes icon;
    int64_t timestamp_ms;
    uint32_t close_reason;
    uint32_t reserved;
    struct lp3_platform_string replaces_id;
    struct lp3_platform_string category;
    struct lp3_platform_string icon_name;
};

struct lp3_platform_notification_command_v1 {
    uint32_t struct_size;
    uint32_t command;
    struct lp3_platform_string id;
};

struct lp3_platform_message_v1 {
    uint32_t struct_size;
    uint32_t flags;
    struct lp3_platform_string conversation_id;
    struct lp3_platform_string recipient;
    struct lp3_platform_string text;
};

struct lp3_platform_call_command_v1 {
    uint32_t struct_size;
    uint32_t command;
    struct lp3_platform_string call_id;
};

struct lp3_platform_calendar_query_v1 {
    uint32_t struct_size;
    uint32_t max_records;
    int64_t start_ms;
    int64_t end_ms;
};

struct lp3_platform_calendar_event_v1 {
    uint32_t struct_size;
    uint32_t flags;
    int64_t start_ms;
    int64_t end_ms;
    struct lp3_platform_string id;
    struct lp3_platform_string title;
    struct lp3_platform_string location;
};

struct lp3_platform_contact_query_v1 {
    uint32_t struct_size;
    uint32_t max_records;
    struct lp3_platform_string query;
};

struct lp3_platform_contact_v1 {
    uint32_t struct_size;
    uint32_t flags;
    struct lp3_platform_string id;
    struct lp3_platform_string display_name;
    struct lp3_platform_string phone_number;
    struct lp3_platform_bytes avatar;
};

struct lp3_platform_location_request_v1 {
    uint32_t struct_size;
    uint32_t accuracy;
    uint32_t timeout_ms;
};

struct lp3_platform_location_v1 {
    uint32_t struct_size;
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t accuracy_m;
    int64_t timestamp_ms;
};

struct lp3_platform_media_state_v1 {
    uint32_t struct_size;
    /* LP3_PLATFORM_MEDIA_SYSTEM_VOLUME is the only provider-defined flag. */
    uint32_t flags;
    /* Valid only with LP3_PLATFORM_MEDIA_SYSTEM_VOLUME; range 0 through 100. */
    int32_t volume_percent;
    /* Generic MPRIS data; Sailfish providers must leave these views empty. */
    struct lp3_platform_string title;
    struct lp3_platform_string artist;
    struct lp3_platform_string album;
};

struct lp3_platform_call_state_v1 {
    uint32_t struct_size;
    uint32_t state;
    struct lp3_platform_string id;
    struct lp3_platform_string name;
    struct lp3_platform_string number;
};

struct lp3_platform_time_state_v1 {
    uint32_t struct_size;
    int32_t utc_offset_seconds;
    int64_t unix_ms;
    uint32_t is_24_hour;
};

struct lp3_platform_device_state_v1 {
    uint32_t struct_size;
    uint32_t battery_percent;
    uint32_t charging;
    uint32_t locked;
    uint32_t silent;
};

struct lp3_platform_profile_request_v1 {
    uint32_t struct_size;
    struct lp3_platform_string profile_id;
};

struct lp3_platform_profile_v1 {
    uint32_t struct_size;
    struct lp3_platform_string active_profile_id;
};

struct lp3_platform_provider_status_v1 {
    uint32_t struct_size;
    uint32_t state;
    uint64_t ready_domains;
    uint64_t degraded_domains;
    uint64_t failed_domains;
    int64_t helper_pid;
    struct lp3_platform_string error;
};

struct lp3_platform_event_v1 {
    uint32_t struct_size;
    uint32_t type;
    uint64_t request_id;
    int32_t status;
    uint32_t reserved;
    const struct lp3_platform_notification_v1 *notification;
    const struct lp3_platform_message_v1 *message;
    const struct lp3_platform_media_state_v1 *media;
    const struct lp3_platform_call_state_v1 *call;
    const struct lp3_platform_calendar_event_v1 *calendar;
    const struct lp3_platform_contact_v1 *contact;
    const struct lp3_platform_location_v1 *location;
    const struct lp3_platform_time_state_v1 *time;
    const struct lp3_platform_device_state_v1 *device_state;
    const struct lp3_platform_profile_v1 *profile;
    const struct lp3_platform_provider_status_v1 *provider_status;
};

struct lp3_platform_provider_info_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t domains;
    struct lp3_platform_string provider_name;
    struct lp3_platform_string build_id;
};

typedef void (*lp3_platform_event_callback)(
    void *context,
    const struct lp3_platform_event_v1 *event);

struct lp3_platform_host_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    lp3_platform_event_callback event;
    void *event_context;
    uint32_t max_queued_events;
    uint32_t max_payload_bytes;
};

struct lp3_platform_instance;

struct lp3_platform_api_v1 {
    uint32_t struct_size;
    struct lp3_platform_provider_info_v1 info;

    int32_t (*probe)(const struct lp3_platform_host_v1 *host);
    int32_t (*create)(const struct lp3_platform_host_v1 *host,
                      struct lp3_platform_instance **instance);
    /* A failed start must leave no provider thread or callback running. */
    int32_t (*start)(struct lp3_platform_instance *instance);
    int32_t (*cancel)(struct lp3_platform_instance *instance,
                      uint64_t request_id);
    /*
     * This call is idempotent and must synchronously stop every callback and
     * join every provider-owned thread before it returns LP3_PLATFORM_OK.
     * The host calls destroy immediately afterwards and may dlclose the DSO,
     * so no provider instruction may execute after a successful return.
     */
    int32_t (*request_stop)(struct lp3_platform_instance *instance);
    void (*destroy)(struct lp3_platform_instance *instance);

    int32_t (*post_notification)(struct lp3_platform_instance *instance,
                                 uint64_t request_id,
                                 const struct lp3_platform_notification_v1 *notification);
    int32_t (*reply_message)(struct lp3_platform_instance *instance,
                             uint64_t request_id,
                             const struct lp3_platform_message_v1 *message);
    int32_t (*media_command)(struct lp3_platform_instance *instance,
                             uint64_t request_id,
                             uint32_t command);
    int32_t (*call_command)(struct lp3_platform_instance *instance,
                            uint64_t request_id,
                            const struct lp3_platform_call_command_v1 *call);
    int32_t (*calendar_query)(struct lp3_platform_instance *instance,
                              uint64_t request_id,
                              const struct lp3_platform_calendar_query_v1 *query);
    int32_t (*contact_query)(struct lp3_platform_instance *instance,
                             uint64_t request_id,
                             const struct lp3_platform_contact_query_v1 *query);
    int32_t (*location_query)(struct lp3_platform_instance *instance,
                              uint64_t request_id,
                              const struct lp3_platform_location_request_v1 *request);
    int32_t (*set_profile)(struct lp3_platform_instance *instance,
                           uint64_t request_id,
                           const struct lp3_platform_profile_request_v1 *profile);
    int32_t (*get_time_state)(struct lp3_platform_instance *instance,
                              struct lp3_platform_time_state_v1 *state);
    int32_t (*get_device_state)(struct lp3_platform_instance *instance,
                                struct lp3_platform_device_state_v1 *state);
    int32_t (*get_status)(struct lp3_platform_instance *instance,
                          struct lp3_platform_provider_status_v1 *status);

    /* Added in ABI 1.1. Commands contain only a provider-observed ID. */
    int32_t (*notification_command)(
        struct lp3_platform_instance *instance,
        uint64_t request_id,
        const struct lp3_platform_notification_command_v1 *command);
};

int32_t lp3_platform_get_api(uint32_t host_abi_major,
                             uint32_t host_abi_minor,
                             const struct lp3_platform_api_v1 **api);

#ifdef __cplusplus
}
#endif

#endif /* LIBPEBBLE3D_PLATFORM_H */
