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

static unsigned int notification_command_count;
static unsigned int reply_message_count;
static unsigned int call_command_count;
static unsigned int media_command_count;
static struct JNINativeInterface_ test_jni_functions;
static JNIEnv test_env = &test_jni_functions;
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

static int32_t test_notification_command(
    struct lp3_platform_instance *instance, uint64_t request_id,
    const struct lp3_platform_notification_command_v1 *command) {
    (void)instance;
    pthread_mutex_lock(&event_lock);
    assert(provider_command_dispatches == 1);
    pthread_mutex_unlock(&event_lock);
    assert(request_id != 0);
    assert(command->command == LP3_PLATFORM_NOTIFICATION_DISMISS);
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

static void set_up_command_provider(void) {
    memset(&command_api, 0, sizeof(command_api));
    command_api.struct_size = sizeof(command_api);
    command_api.info.domains = LP3_PLATFORM_DOMAIN_NOTIFICATIONS |
        LP3_PLATFORM_DOMAIN_MESSAGING | LP3_PLATFORM_DOMAIN_CALLS |
        LP3_PLATFORM_DOMAIN_MEDIA;
    command_api.notification_command = test_notification_command;
    command_api.reply_message = test_reply_message;
    command_api.call_command = test_call_command;
    command_api.media_command = test_media_command;
    memset(&loader, 0, sizeof(loader));
    loader.api = &command_api;
    loader.instance = (struct lp3_platform_instance *)&command_api;
    loader.next_request_id = 1;
    notification_command_count = 0;
    reply_message_count = 0;
    call_command_count = 0;
    media_command_count = 0;
    memset(&test_jni_functions, 0, sizeof(test_jni_functions));
    test_jni_functions.GetStringUTFLength = test_get_string_utf_length;
    test_jni_functions.GetStringUTFChars = test_get_string_utf_chars;
    test_jni_functions.ReleaseStringUTFChars = test_release_string_utf_chars;
    test_jni_functions.GetStringLength = test_get_string_length;
    test_jni_functions.GetStringChars = test_get_string_chars;
    test_jni_functions.ReleaseStringChars = test_release_string_chars;
    test_jni_functions.ExceptionCheck = test_exception_check;
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

static void enqueue_reset(void) {
    struct lp3_platform_provider_status_v1 status;
    struct lp3_platform_event_v1 event;

    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.state = LP3_PLATFORM_PROVIDER_DEGRADED;
    status.degraded_domains = LP3_PLATFORM_DOMAIN_ALL;
    status.error = string("org.rockpool.Error.ProviderUnavailable");
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = LP3_PLATFORM_EVENT_PROVIDER_STATUS;
    event.status = LP3_PLATFORM_OK;
    event.provider_status = &status;
    provider_event(NULL, &event);
}

int main(void) {
    unsigned int index;
    pthread_t reset_thread;
    jchar maximum_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 1];
    jchar oversized_reply_text[LP3_PLATFORM_MESSAGE_TEXT_MAX + 2];
    static const jchar reply_text[] = {
        'O', 'n', ' ', 'm', 'y', ' ', 'w', 'a', 'y', ' ',
        0xd83d, 0xde00, 0,
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
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)reply_text) ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_callCommand(
        &test_env, NULL, LP3_PLATFORM_CALL_ANSWER, (jstring)"call_1") ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_mediaCommand(
        &test_env, NULL, LP3_PLATFORM_MEDIA_VOLUME_UP) == LP3_PLATFORM_OK);
    assert(notification_command_count == 1);
    assert(reply_message_count == 1);
    assert(call_command_count == 1);
    assert(media_command_count == 1);
    assert(media_event_count == 1);
    assert(media_event_queue[media_event_head].volume_percent == 55);

    /* A retired domain rejects only its corresponding command. */
    pthread_mutex_lock(&event_lock);
    event_failed_domains = LP3_PLATFORM_DOMAIN_CALLS |
        LP3_PLATFORM_DOMAIN_MESSAGING;
    pthread_mutex_unlock(&event_lock);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_notificationCommand(
        &test_env, NULL, LP3_PLATFORM_NOTIFICATION_DISMISS, (jstring)"42") ==
        LP3_PLATFORM_OK);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_callCommand(
        &test_env, NULL, LP3_PLATFORM_CALL_ANSWER, (jstring)"call_1") ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage(
        &test_env, NULL, (jstring)"42", (jstring)reply_text) ==
        LP3_PLATFORM_UNAVAILABLE);
    assert(Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_mediaCommand(
        &test_env, NULL, LP3_PLATFORM_MEDIA_VOLUME_UP) == LP3_PLATFORM_OK);
    assert(notification_command_count == 2);
    assert(reply_message_count == 1);
    assert(call_command_count == 1);
    assert(media_command_count == 2);
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
    assert(notification_command_count == 2);
    assert(reply_message_count == 2);
    assert(call_command_count == 1);
    assert(media_command_count == 2);
    assert(pthread_mutex_trylock(&event_lock) == 0);
    pthread_mutex_unlock(&event_lock);

    return 0;
}
