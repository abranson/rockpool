/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * White-box coverage for the loader's cross-generation event queues.  Keeping
 * this beside the loader lets the test exercise the ABI callback exactly as a
 * provider does, without a JNI runtime or a dynamically loaded provider.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <string.h>

#include "../native/platform_loader.c"

static void enqueue_reset(void);
static void enqueue_media(int32_t volume_percent);
static void enqueue_location(uint64_t request_id, int32_t status,
                             const struct lp3_platform_location_v1 *location);

static unsigned int notification_command_count;
static unsigned int reply_message_count;
static unsigned int send_message_count;
static unsigned int call_command_count;
static unsigned int media_command_count;
static unsigned int remove_pebble_bond_count;
static unsigned int location_query_count;
static unsigned int location_cancel_count;
static int location_query_synchronous;
static struct JNINativeInterface_ test_jni_functions;
static JNIEnv test_env = &test_jni_functions;
static jlong test_long_array[LP3_MAX_QUEUED_EVENTS * 6];
static struct lp3_platform_api_v1 command_api;
static pthread_mutex_t reset_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reset_thread_condition = PTHREAD_COND_INITIALIZER;
static int reset_thread_started;
static int reset_thread_finished;

static void *enqueue_reset_thread(void *context) {
    (void)context;

    pthread_mutex_lock(&reset_thread_lock);
    reset_thread_started = 1;
    pthread_cond_signal(&reset_thread_condition);
    pthread_mutex_unlock(&reset_thread_lock);
    enqueue_reset();
    pthread_mutex_lock(&reset_thread_lock);
    reset_thread_finished = 1;
    pthread_cond_signal(&reset_thread_condition);
    pthread_mutex_unlock(&reset_thread_lock);
    return NULL;
}

static jsize test_get_string_utf_length(JNIEnv *env, jstring value) {
    (void)env;
    return (jsize)strlen((const char *)value);
}

static const char *test_get_string_utf_chars(JNIEnv *env, jstring value,
                                              jboolean *copied) {
    (void)env;
    if (copied != NULL) {
        *copied = JNI_FALSE;
    }
    return (const char *)value;
}

static void test_release_string_utf_chars(JNIEnv *env, jstring value,
                                          const char *text) {
    (void)env;
    (void)value;
    (void)text;
}

static jsize test_get_string_length(JNIEnv *env, jstring value) {
    const jchar *text = (const jchar *)value;
    jsize length = 0;
    (void)env;
    while (text[length] != 0) {
        ++length;
    }
    return length;
}

static const jchar *test_get_string_chars(JNIEnv *env, jstring value,
                                           jboolean *copied) {
    (void)env;
    if (copied != NULL) {
        *copied = JNI_FALSE;
    }
    return (const jchar *)value;
}

static void test_release_string_chars(JNIEnv *env, jstring value,
                                      const jchar *text) {
    (void)env;
    (void)value;
    (void)text;
}

static jboolean test_exception_check(JNIEnv *env) {
    (void)env;
    return JNI_FALSE;
}

static jlongArray test_new_long_array(JNIEnv *env, jsize length) {
    (void)env;
    assert(length >= 0 && (size_t)length <=
           sizeof(test_long_array) / sizeof(test_long_array[0]));
    memset(test_long_array, 0, sizeof(test_long_array));
    return (jlongArray)test_long_array;
}

static void test_set_long_array_region(JNIEnv *env, jlongArray array,
                                       jsize start, jsize length,
                                       const jlong *values) {
    (void)env;
    assert(array == (jlongArray)test_long_array);
    assert(start >= 0 && length >= 0 &&
           (size_t)(start + length) <=
               sizeof(test_long_array) / sizeof(test_long_array[0]));
    memcpy(test_long_array + start, values, (size_t)length * sizeof(*values));
}

static int32_t test_notification_command(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_notification_command_v1 *command) {
    (void)instance;
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    pthread_mutex_unlock(&event_lock);
    assert(request_id != 0);
    assert(command->command == LP3_PLATFORM_NOTIFICATION_DISMISS ||
           command->command == LP3_PLATFORM_NOTIFICATION_OPEN);
    ++notification_command_count;
    return LP3_PLATFORM_OK;
}

static int32_t test_reply_message(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_message_v1 *message) {
    static const char expected_text[] = "On my way \xf0\x9f\x98\x80";
    (void)instance;
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    pthread_mutex_unlock(&event_lock);
    assert(request_id != 0);
    assert(message->flags == 0);
    assert(message->conversation_id.size == 2);
    assert(memcmp(message->conversation_id.data, "42", 2) == 0);
    assert(message->recipient.size == 0);
    assert(message->recipient.data == NULL);
    assert((message->text.size == sizeof(expected_text) - 1 &&
            memcmp(message->text.data, expected_text,
                   sizeof(expected_text) - 1) == 0) ||
           (message->text.size == LP3_PLATFORM_MESSAGE_TEXT_MAX &&
            strspn(message->text.data, "x") ==
                LP3_PLATFORM_MESSAGE_TEXT_MAX));
    ++reply_message_count;
    return LP3_PLATFORM_OK;
}

static int32_t test_send_message(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_outgoing_message_v1 *message) {
    static const char account[] =
        "/org/freedesktop/Telepathy/Account/ring/tel/ril_0";
    (void)instance;
    assert(request_id != 0 && message->flags == 0);
    assert(message->account_id.size == sizeof(account) - 1);
    assert(memcmp(message->account_id.data, account, sizeof(account) - 1) == 0);
    assert(message->recipient.size == 7);
    assert(memcmp(message->recipient.data, "+358123", 7) == 0);
    assert(message->text.size == 5);
    assert(memcmp(message->text.data, "Hello", 5) == 0);
    ++send_message_count;
    return LP3_PLATFORM_OK;
}

static int32_t test_call_command(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_call_command_v1 *command) {
    (void)instance;
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    pthread_mutex_unlock(&event_lock);
    assert(request_id != 0);
    assert(command->command == LP3_PLATFORM_CALL_ANSWER);
    ++call_command_count;
    return LP3_PLATFORM_OK;
}

static int32_t test_media_command(struct lp3_platform_instance *instance,
                                  uint64_t request_id, uint32_t command) {
    (void)instance;
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    pthread_mutex_unlock(&event_lock);
    assert(request_id != 0);
    assert(command == LP3_PLATFORM_MEDIA_VOLUME_UP);
    /* Real MainVolumeMonitor emits its state event before the Complete reply. */
    enqueue_media(55);
    ++media_command_count;
    return LP3_PLATFORM_OK;
}

static int32_t test_remove_pebble_bond(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_pebble_bond_v1 *bond) {
    static const uint8_t expected[] = { 0xd0, 0x81, 0x0a, 0xd4, 0xd7, 0xcd };
    (void)instance;
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    pthread_mutex_unlock(&event_lock);
    assert(request_id != 0);
    assert(bond->struct_size == sizeof(*bond));
    assert(bond->adapter_index == 0);
    assert(bond->reserved[0] == 0 && bond->reserved[1] == 0);
    assert(memcmp(bond->address, expected, sizeof(expected)) == 0);
    ++remove_pebble_bond_count;
    return LP3_PLATFORM_OK;
}

static int32_t test_location_query(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_location_request_v1 *request) {
    (void)instance;
    assert(request_id != 0);
    assert(request->struct_size == sizeof(*request));
    assert(request->accuracy == LP3_PLATFORM_LOCATION_FINE);
    assert(request->timeout_ms == 1000);
    ++location_query_count;
    if (location_query_synchronous) {
        struct lp3_platform_location_v1 location;
        memset(&location, 0, sizeof(location));
        location.struct_size = sizeof(location);
        location.latitude_e7 = 488566000;
        location.longitude_e7 = 23522000;
        location.accuracy_m = 12;
        location.timestamp_ms = 1234;
        enqueue_location(request_id, LP3_PLATFORM_OK, &location);
    }
    return LP3_PLATFORM_OK;
}

static int32_t test_location_cancel(struct lp3_platform_instance *instance,
                                    uint64_t request_id) {
    (void)instance;
    assert(request_id != 0);
    ++location_cancel_count;
    return LP3_PLATFORM_OK;
}

static void set_up_command_provider(void) {
    memset(&command_api, 0, sizeof(command_api));
    command_api.struct_size = sizeof(command_api);
    command_api.info.abi_minor = LP3_PLATFORM_ABI_MINOR;
    command_api.info.domains = LP3_PLATFORM_DOMAIN_NOTIFICATIONS |
        LP3_PLATFORM_DOMAIN_MESSAGING | LP3_PLATFORM_DOMAIN_CALLS |
        LP3_PLATFORM_DOMAIN_MEDIA | LP3_PLATFORM_DOMAIN_LOCATION;
    command_api.notification_command = test_notification_command;
    command_api.reply_message = test_reply_message;
    command_api.send_message = test_send_message;
    command_api.call_command = test_call_command;
    command_api.media_command = test_media_command;
    command_api.remove_pebble_bond = test_remove_pebble_bond;
    command_api.location_query = test_location_query;
    command_api.cancel = test_location_cancel;
    memset(&loader, 0, sizeof(loader));
    loader.api = &command_api;
    loader.instance = (struct lp3_platform_instance *)&command_api;
    loader.next_request_id = 1;
    notification_command_count = 0;
    reply_message_count = 0;
    send_message_count = 0;
    call_command_count = 0;
    media_command_count = 0;
    remove_pebble_bond_count = 0;
    location_query_count = 0;
    location_cancel_count = 0;
    location_query_synchronous = 0;
    memset(&test_jni_functions, 0, sizeof(test_jni_functions));
    test_jni_functions.GetStringUTFLength = test_get_string_utf_length;
    test_jni_functions.GetStringUTFChars = test_get_string_utf_chars;
    test_jni_functions.ReleaseStringUTFChars = test_release_string_utf_chars;
    test_jni_functions.GetStringLength = test_get_string_length;
    test_jni_functions.GetStringChars = test_get_string_chars;
    test_jni_functions.ReleaseStringChars = test_release_string_chars;
    test_jni_functions.ExceptionCheck = test_exception_check;
    test_jni_functions.NewLongArray = test_new_long_array;
    test_jni_functions.SetLongArrayRegion = test_set_long_array_region;
}

static struct lp3_platform_string string(const char *value) {
    struct lp3_platform_string result;

    result.data = value;
    result.size = (uint32_t)strlen(value);
    return result;
}

static void enqueue_call(const char *id) {
    struct lp3_platform_call_state_v1 call;
    struct lp3_platform_event_v1 event;

    memset(&call, 0, sizeof(call));
    call.struct_size = sizeof(call);
    call.state = LP3_PLATFORM_CALL_RINGING;
    call.id = string(id);
    call.name = string("Alice");
    call.number = string("+123");
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_CALL;
    event.status = LP3_PLATFORM_OK;
    event.call = &call;
    provider_event(NULL, &event);
}

static void enqueue_media(int32_t volume_percent) {
    struct lp3_platform_media_state_v1 media;
    struct lp3_platform_event_v1 event;

    memset(&media, 0, sizeof(media));
    media.struct_size = sizeof(media);
    media.flags = LP3_PLATFORM_MEDIA_SYSTEM_VOLUME;
    media.volume_percent = volume_percent;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_MEDIA;
    event.status = LP3_PLATFORM_OK;
    event.media = &media;
    provider_event(NULL, &event);
}

static void enqueue_location(uint64_t request_id, int32_t status,
                             const struct lp3_platform_location_v1 *location) {
    struct lp3_platform_event_v1 event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_LOCATION;
    event.request_id = request_id;
    event.status = status;
    event.location = location;
    provider_event(NULL, &event);
}

static struct lp3_platform_location_v1 test_valid_location(void) {
    struct lp3_platform_location_v1 location;
    memset(&location, 0, sizeof(location));
    location.struct_size = sizeof(location);
    location.latitude_e7 = 488566000;
    location.longitude_e7 = 23522000;
    location.accuracy_m = 12;
    location.timestamp_ms = 1234;
    return location;
}

static void enqueue_reset(void) {
    struct lp3_platform_provider_status_v1 status;
    struct lp3_platform_event_v1 event;

    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.state = LP3_PLATFORM_PROVIDER_DEGRADED;
    status.degraded_domains = LP3_PLATFORM_DOMAIN_ALL;
    status.error = string("io.rebble.libpebble3.Error.ProviderUnavailable");
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_PROVIDER_STATUS;
    event.status = LP3_PLATFORM_OK;
    event.provider_status = &status;
    provider_event(NULL, &event);
}

int main(void) {
    unsigned int index;
    uint64_t evicted_location_request_id = 0;
    uint64_t location_request_id;
    pthread_t reset_thread;
    jchar maximum_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 1];
    jchar oversized_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 2];
    static const jchar reply_text[] = {
        'O', 'n', ' ', 'm', 'y', ' ', 'w', 'a', 'y', ' ',
        0xd83d, 0xde00, 0,
    };
    static const jchar send_account[] = {
        '/', 'o', 'r', 'g', '/', 'f', 'r', 'e', 'e', 'd', 'e', 's', 'k', 't',
        'o', 'p', '/', 'T', 'e', 'l', 'e', 'p', 'a', 't', 'h', 'y', '/', 'A',
        'c', 'c', 'o', 'u', 'n', 't', '/', 'r', 'i', 'n', 'g', '/', 't', 'e',
        'l', '/', 'r', 'i', 'l', '_', '0', 0
    };
    static const jchar send_recipient[] = {
        '+', '3', '5', '8', '1', '2', '3', 0
    };
    static const jchar send_text[] = { 'H', 'e', 'l', 'l', 'o', 0 };
    static const jchar pebble_address[] = {
        'D', '0', ':', '8', '1', ':', '0', 'A', ':', 'D', '4', ':', 'D', '7',
        ':', 'C', 'D', 0
    };
    static const jchar invalid_reply_text[] = { 0xd83d, 0 };
    static const jchar invalid_low_surrogate[] = { 0xdc00, 0 };

    for (index = 0; index < LP3_PLATFORM_MESSAGE_TEXT_MAX; ++index) {
        maximum_reply_text[index] = 'x';
        oversized_reply_text[index] = 'x';
    }
    maximum_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX] = 0;
    oversized_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX] = 'x';
    oversized_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 1] = 0;

    reset_events();

    /* JNI drains may nest the queue helpers only inside one explicit lease. */
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_beginEventBatch(
        NULL, NULL));
    assert(!Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_beginEventBatch(
        NULL, NULL));
    enqueue_call("leased_call");
    enqueue_media(42);
    assert(call_event_count == 1);
    assert(media_event_count == 1);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_endEventBatch(
        NULL, NULL));
    assert(!Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_endEventBatch(
        NULL, NULL));
    reset_events();

    /* An old-helper call must not cross the replacement generation barrier. */
    enqueue_call("old_call");
    enqueue_media(10);
    assert(call_event_count == 1);
    assert(media_event_count == 1);
    enqueue_reset();
    assert(provider_reset_pending);
    assert(event_count == 0);
    assert(notification_event_count == 0);
    assert(call_event_count == 0);
    assert(media_event_count == 0);

    /* A replacement-helper call is retained after that one reset marker. */
    enqueue_call("new_call");
    enqueue_media(65);
    assert(call_event_count == 1);
    assert(media_event_count == 1);
    assert(strcmp(call_event_queue[call_event_head].id, "new_call") == 0);
    assert(media_event_queue[media_event_head].volume_percent == 65);

    /* Consecutive resets coalesce and discard an intermediate generation. */
    enqueue_reset();
    assert(provider_reset_pending);
    assert(call_event_count == 0);
    assert(media_event_count == 0);
    enqueue_call("latest_call");
    enqueue_media(80);
    assert(call_event_count == 1);
    assert(media_event_count == 1);
    assert(strcmp(call_event_queue[call_event_head].id, "latest_call") == 0);
    assert(media_event_queue[media_event_head].volume_percent == 80);

    /* Level-triggered volume bursts coalesce without degrading Media. */
    for (index = 0; index < LP3_MAX_QUEUED_EVENTS * 2; ++index) {
        enqueue_media((int32_t)(index % 101));
    }
    assert(media_event_count == 1);
    assert(media_event_queue[media_event_head].volume_percent ==
           (int32_t)((LP3_MAX_QUEUED_EVENTS * 2 - 1) % 101));
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_MEDIA) == 0);

    /* Malformed Media state fails only that domain and clears its queue. */
    enqueue_media(101);
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_MEDIA) != 0);
    assert(media_event_count == 0);
    assert(call_event_count == 1);

    /* A callback cannot publish a reset while a dispatch gate is held. */
    reset_events();
    reset_thread_started = 0;
    reset_thread_finished = 0;
    pthread_mutex_lock(&loader_lock);
    assert(begin_provider_command_dispatch(LP3_PLATFORM_DOMAIN_CALLS));
    assert(pthread_create(&reset_thread, NULL, enqueue_reset_thread, NULL) == 0);
    pthread_mutex_lock(&reset_thread_lock);
    while (!reset_thread_started) {
        pthread_cond_wait(&reset_thread_condition, &reset_thread_lock);
    }
    assert(!reset_thread_finished);
    pthread_mutex_unlock(&reset_thread_lock);
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    assert(!provider_reset_pending);
    pthread_mutex_unlock(&event_lock);
    end_provider_command_dispatch();
    pthread_mutex_unlock(&loader_lock);
    pthread_mutex_lock(&reset_thread_lock);
    while (!reset_thread_finished) {
        pthread_cond_wait(&reset_thread_condition, &reset_thread_lock);
    }
    pthread_mutex_unlock(&reset_thread_lock);
    assert(pthread_join(reset_thread, NULL) == 0);
    assert(provider_reset_pending);

    /* Each stateful command reserves its generation through provider dispatch. */
    reset_events();
    set_up_command_provider();
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
        &test_env, NULL, LP3_PLATFORM_NOTIFICATION_DISMISS, (jstring)"42") ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
        &test_env, NULL, LP3_PLATFORM_NOTIFICATION_OPEN, (jstring)"42") ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)reply_text) ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_sendMessage(
        &test_env, NULL, (jstring)send_account, (jstring)send_recipient,
        (jstring)send_text) == LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_callCommand(
        &test_env, NULL, LP3_PLATFORM_CALL_ANSWER, (jstring)"call_1") ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_mediaCommand(
        &test_env, NULL, LP3_PLATFORM_MEDIA_VOLUME_UP) == LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_removePebbleBond(
        &test_env, NULL, 0, (jstring)pebble_address) == LP3_PLATFORM_OK);
    assert(notification_command_count == 2);
    assert(reply_message_count == 1);
    assert(send_message_count == 1);
    assert(call_command_count == 1);
    assert(media_command_count == 1);
    assert(remove_pebble_bond_count == 1);
    assert(media_event_count == 1);
    assert(media_event_queue[media_event_head].volume_percent == 55);

    /* Location completions are correlated, bounded, and do not affect Media. */
    reset_events();
    location_query_synchronous = 1;
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    assert(test_long_array[0] == LP3_PLATFORM_OK && location_event_count == 1 &&
           location_outstanding_count == 0);
    Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainLocationEvents(
        &test_env, NULL);
    location_query_synchronous = 0;
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000) ==
        (jlongArray)test_long_array);
    assert(test_long_array[0] == LP3_PLATFORM_OK && test_long_array[1] > 0);
    location_request_id = (uint64_t)test_long_array[1];
    assert(location_query_count == 2 && location_outstanding_count == 1);
    {
        struct lp3_platform_location_v1 location = test_valid_location();
        enqueue_location(location_request_id, LP3_PLATFORM_OK, &location);
    }
    assert(location_outstanding_count == 0 && location_event_count == 1);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainLocationEvents(
        &test_env, NULL) == (jlongArray)test_long_array);
    assert(test_long_array[0] == (jlong)location_request_id &&
           test_long_array[1] == LP3_PLATFORM_OK &&
           test_long_array[2] == 488566000 && test_long_array[3] == 23522000 &&
           test_long_array[4] == 12 && test_long_array[5] == 1234);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    location_request_id = (uint64_t)test_long_array[1];
    enqueue_location(location_request_id, LP3_PLATFORM_UNAVAILABLE, NULL);
    Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainLocationEvents(
        &test_env, NULL);
    assert(test_long_array[0] == (jlong)location_request_id &&
           test_long_array[1] == LP3_PLATFORM_UNAVAILABLE &&
           test_long_array[2] == 0 && test_long_array[3] == 0 &&
           test_long_array[4] == 0 && test_long_array[5] == 0);

    /* Unknown and malformed completion data retire Location only. */
    reset_events();
    enqueue_location(99, LP3_PLATFORM_UNAVAILABLE, NULL);
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_LOCATION) == 0);
    assert(media_event_count == 0);
    {
        struct lp3_platform_location_v1 location = test_valid_location();
        location.latitude_e7 = 900000001;
        enqueue_location(100, LP3_PLATFORM_OK, &location);
    }
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_LOCATION) == 0);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    location_request_id = (uint64_t)test_long_array[1];
    {
        struct lp3_platform_location_v1 location = test_valid_location();
        location.latitude_e7 = 900000001;
        enqueue_location(location_request_id, LP3_PLATFORM_OK, &location);
    }
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_LOCATION) != 0);
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_MEDIA) == 0);
    reset_events();
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    location_request_id = (uint64_t)test_long_array[1];
    {
        struct lp3_platform_location_v1 location = test_valid_location();
        location.timestamp_ms = 0;
        enqueue_location(location_request_id, LP3_PLATFORM_OK, &location);
    }
    assert((event_failed_domains & LP3_PLATFORM_DOMAIN_LOCATION) != 0);

    /* Cancel records a bounded tombstone before provider cancellation. */
    reset_events();
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    location_request_id = (uint64_t)test_long_array[1];
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_cancelLocation(
        &test_env, NULL, (jlong)location_request_id) == LP3_PLATFORM_OK);
    assert(location_cancel_count == 1 && location_outstanding_count == 0 &&
           location_tombstone_count == 1);
    {
        struct lp3_platform_location_v1 location = test_valid_location();
        enqueue_location(location_request_id, LP3_PLATFORM_OK, &location);
    }
    assert(location_tombstone_count == 0 && location_event_count == 0);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_cancelLocation(
        &test_env, NULL, (jlong)location_request_id) == LP3_PLATFORM_INVALID_ARGUMENT);

    reset_events();
    for (index = 0; index < LP3_MAX_QUEUED_EVENTS + 1; ++index) {
        assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
            &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
        location_request_id = (uint64_t)test_long_array[1];
        if (index == 0) {
            evicted_location_request_id = location_request_id;
        }
        assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_cancelLocation(
            &test_env, NULL, (jlong)location_request_id) == LP3_PLATFORM_OK);
    }
    assert(location_tombstone_count == LP3_MAX_QUEUED_EVENTS);
    {
        struct lp3_platform_location_v1 location = test_valid_location();
        location.timestamp_ms = 0;
        enqueue_location(evicted_location_request_id, LP3_PLATFORM_OK, &location);
    }
    assert(location_event_count == 0 &&
           location_tombstone_count == LP3_MAX_QUEUED_EVENTS &&
           (event_failed_domains & LP3_PLATFORM_DOMAIN_LOCATION) == 0);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    assert(test_long_array[0] == LP3_PLATFORM_OK);

    /* Admission and cancellation retention are bounded by the event capacity. */
    reset_events();
    for (index = 0; index < LP3_MAX_QUEUED_EVENTS; ++index) {
        assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
            &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
        assert(test_long_array[0] == LP3_PLATFORM_OK);
    }
    assert(location_outstanding_count == LP3_MAX_QUEUED_EVENTS);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    assert(test_long_array[0] == LP3_PLATFORM_BUSY);
    reset_events();

    /* Reset clears Location correlation and blocks replacement events to its marker. */
    reset_events();
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 1000));
    enqueue_reset();
    assert(provider_reset_pending && location_outstanding_count == 0 &&
           location_tombstone_count == 0 && location_event_count == 0);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainLocationEvents(
        &test_env, NULL));
    assert(test_long_array[0] == 0);
    Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_drainEvents(
        &test_env, NULL);
    assert(!provider_reset_pending);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_COARSE, 0));
    assert(test_long_array[0] == LP3_PLATFORM_INVALID_ARGUMENT);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, 0, 1000));
    assert(test_long_array[0] == LP3_PLATFORM_INVALID_ARGUMENT);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_locationStart(
        &test_env, NULL, LP3_PLATFORM_LOCATION_FINE, 30001));
    assert(test_long_array[0] == LP3_PLATFORM_INVALID_ARGUMENT);

    /* A retired domain rejects only its corresponding command. */
    pthread_mutex_lock(&event_lock);
    event_failed_domains = LP3_PLATFORM_DOMAIN_CALLS |
        LP3_PLATFORM_DOMAIN_MESSAGING;
    pthread_mutex_unlock(&event_lock);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
        &test_env, NULL, LP3_PLATFORM_NOTIFICATION_DISMISS, (jstring)"42") ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
        &test_env, NULL, LP3_PLATFORM_NOTIFICATION_OPEN, (jstring)"42") ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_callCommand(
        &test_env, NULL, LP3_PLATFORM_CALL_ANSWER, (jstring)"call_1") ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)reply_text) ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_sendMessage(
        &test_env, NULL, (jstring)send_account, (jstring)send_recipient,
        (jstring)send_text) == LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_mediaCommand(
        &test_env, NULL, LP3_PLATFORM_MEDIA_VOLUME_UP) == LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_removePebbleBond(
        &test_env, NULL, 0, (jstring)pebble_address) == LP3_PLATFORM_OK);
    assert(notification_command_count == 3);
    assert(reply_message_count == 1);
    assert(send_message_count == 1);
    assert(call_command_count == 1);
    assert(media_command_count == 2);
    assert(remove_pebble_bond_count == 2);
    reset_events();

    /* Java UTF-16 is converted to strict bounded UTF-8 before provider use. */
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)maximum_reply_text) ==
        LP3_PLATFORM_OK);
    assert(reply_message_count == 2);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)oversized_reply_text) ==
        LP3_PLATFORM_INVALID_ARGUMENT);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)invalid_reply_text) ==
        LP3_PLATFORM_INVALID_ARGUMENT);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)invalid_low_surrogate) ==
        LP3_PLATFORM_INVALID_ARGUMENT);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"not-a-number", (jstring)reply_text) ==
        LP3_PLATFORM_INVALID_ARGUMENT);
    assert(reply_message_count == 2);

    /* A queued reset rejects all commands until JNI drains its marker. */
    enqueue_reset();
    assert(provider_reset_pending);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
        &test_env, NULL, LP3_PLATFORM_NOTIFICATION_DISMISS, (jstring)"42") ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)reply_text) ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_callCommand(
        &test_env, NULL, LP3_PLATFORM_CALL_ANSWER, (jstring)"call_1") ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_mediaCommand(
        &test_env, NULL, LP3_PLATFORM_MEDIA_VOLUME_UP) ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_removePebbleBond(
        &test_env, NULL, 0, (jstring)pebble_address) ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(notification_command_count == 3);
    assert(reply_message_count == 2);
    assert(call_command_count == 1);
    assert(media_command_count == 2);
    assert(remove_pebble_bond_count == 2);
    assert(pthread_mutex_trylock(&event_lock) == 0);
    pthread_mutex_unlock(&event_lock);

    return 0;
}
