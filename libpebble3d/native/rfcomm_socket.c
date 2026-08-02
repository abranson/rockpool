/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interruptible outbound RFCOMM client used by the native-Linux Classic
 * transport.  Pebble's legacy transport is a raw byte stream on channel 1;
 * Bluetooth discovery and bonding remain BlueZ D-Bus responsibilities.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif

#define LP3_RFCOMM_MAX_CHANNEL 30
#define LP3_RFCOMM_MAX_TIMEOUT_MS 120000
#define LP3_RFCOMM_MAX_IO (128 * 1024)

struct lp3_bdaddr {
    uint8_t bytes[6];
} __attribute__((packed));

struct lp3_sockaddr_rc {
    sa_family_t family;
    struct lp3_bdaddr address;
    uint8_t channel;
};

_Static_assert(sizeof(struct lp3_sockaddr_rc) == 10,
               "Linux sockaddr_rc ABI size changed");
_Static_assert(offsetof(struct lp3_sockaddr_rc, address) == 2,
               "Linux sockaddr_rc address offset changed");
_Static_assert(offsetof(struct lp3_sockaddr_rc, channel) == 8,
               "Linux sockaddr_rc channel offset changed");

struct lp3_rfcomm_handle {
    pthread_mutex_t lock;
    pthread_cond_t idle;
    struct lp3_sockaddr_rc remote;
    unsigned int active_calls;
    int socket_fd;
    int cancel_read_fd;
    int cancel_write_fd;
    int closing;
};

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

/* Linux bdaddr_t stores the human-readable address in reverse byte order. */
static int parse_bluetooth_address(const char *value,
                                   struct lp3_bdaddr *result) {
    size_t index;

    if (value == NULL || result == NULL || strlen(value) != 17) {
        return 0;
    }
    for (index = 0; index < 6; ++index) {
        const size_t offset = index * 3;
        int high = hex_nibble(value[offset]);
        int low = hex_nibble(value[offset + 1]);

        if (high < 0 || low < 0 || (index != 5 && value[offset + 2] != ':')) {
            return 0;
        }
        result->bytes[5 - index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static int set_nonblocking_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFL);
    int descriptor_flags = fcntl(fd, F_GETFD);

    if (flags < 0 || descriptor_flags < 0 ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0 ||
        fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
        return 0;
    }
    return 1;
}

static struct lp3_rfcomm_handle *allocate_handle(
    int socket_fd, const struct lp3_sockaddr_rc *remote) {
    struct lp3_rfcomm_handle *handle;
    int cancel_pipe[2] = {-1, -1};

    if (socket_fd < 0 || remote == NULL || !set_nonblocking_cloexec(socket_fd) ||
        pipe2(cancel_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
        return NULL;
    }
    handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        close(cancel_pipe[0]);
        close(cancel_pipe[1]);
        close(socket_fd);
        return NULL;
    }
    if (pthread_mutex_init(&handle->lock, NULL) != 0) {
        close(cancel_pipe[0]);
        close(cancel_pipe[1]);
        close(socket_fd);
        free(handle);
        return NULL;
    }
    if (pthread_cond_init(&handle->idle, NULL) != 0) {
        pthread_mutex_destroy(&handle->lock);
        close(cancel_pipe[0]);
        close(cancel_pipe[1]);
        close(socket_fd);
        free(handle);
        return NULL;
    }
    handle->remote = *remote;
    handle->socket_fd = socket_fd;
    handle->cancel_read_fd = cancel_pipe[0];
    handle->cancel_write_fd = cancel_pipe[1];
    return handle;
}

static struct lp3_rfcomm_handle *create_handle(const char *address,
                                                int channel) {
    struct lp3_sockaddr_rc remote;
    int socket_fd;

    memset(&remote, 0, sizeof(remote));
    if (channel < 1 || channel > LP3_RFCOMM_MAX_CHANNEL ||
        !parse_bluetooth_address(address, &remote.address)) {
        return NULL;
    }
    remote.family = AF_BLUETOOTH;
    remote.channel = (uint8_t)channel;
    socket_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (socket_fd < 0) {
        return NULL;
    }
    return allocate_handle(socket_fd, &remote);
}

static int begin_call(struct lp3_rfcomm_handle *handle, int *socket_fd,
                      int *cancel_fd) {
    if (handle == NULL || socket_fd == NULL || cancel_fd == NULL) {
        return -EINVAL;
    }
    pthread_mutex_lock(&handle->lock);
    if (handle->closing) {
        pthread_mutex_unlock(&handle->lock);
        return -ECANCELED;
    }
    ++handle->active_calls;
    *socket_fd = handle->socket_fd;
    *cancel_fd = handle->cancel_read_fd;
    pthread_mutex_unlock(&handle->lock);
    return 0;
}

static void end_call(struct lp3_rfcomm_handle *handle) {
    pthread_mutex_lock(&handle->lock);
    if (--handle->active_calls == 0) {
        pthread_cond_broadcast(&handle->idle);
    }
    pthread_mutex_unlock(&handle->lock);
}

static int is_closing(struct lp3_rfcomm_handle *handle) {
    int closing;

    pthread_mutex_lock(&handle->lock);
    closing = handle->closing;
    pthread_mutex_unlock(&handle->lock);
    return closing;
}

static int64_t monotonic_ms(void) {
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return -1;
    }
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static int wait_for_socket(int socket_fd, int cancel_fd, short events,
                           int timeout_ms) {
    struct pollfd descriptors[2];
    int64_t deadline = -1;

    if (timeout_ms >= 0) {
        int64_t now = monotonic_ms();
        if (now < 0) {
            return -errno;
        }
        deadline = now + timeout_ms;
    }
    descriptors[0].fd = socket_fd;
    descriptors[0].events = events;
    descriptors[1].fd = cancel_fd;
    descriptors[1].events = POLLIN;
    for (;;) {
        int remaining = -1;
        int result;

        descriptors[0].revents = 0;
        descriptors[1].revents = 0;
        if (deadline >= 0) {
            int64_t now = monotonic_ms();
            int64_t delta;

            if (now < 0) {
                return -errno;
            }
            delta = deadline - now;
            if (delta <= 0) {
                return -ETIMEDOUT;
            }
            remaining = delta > INT32_MAX ? INT32_MAX : (int)delta;
        }
        result = poll(descriptors, 2, remaining);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return -errno;
        }
        if (result == 0) {
            return -ETIMEDOUT;
        }
        if (descriptors[1].revents != 0) {
            return -ECANCELED;
        }
        if (descriptors[0].revents & events) {
            return 0;
        }
        if (descriptors[0].revents & POLLNVAL) {
            return -EBADF;
        }
        if (descriptors[0].revents & (POLLERR | POLLHUP)) {
            return -ECONNRESET;
        }
    }
}

static int rfcomm_connect(struct lp3_rfcomm_handle *handle, int timeout_ms) {
    int socket_fd;
    int cancel_fd;
    int result = begin_call(handle, &socket_fd, &cancel_fd);

    if (result != 0) {
        return result;
    }
    if (connect(socket_fd, (const struct sockaddr *)&handle->remote,
                sizeof(handle->remote)) != 0) {
        if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            result = -errno;
            goto done;
        }
        result = wait_for_socket(socket_fd, cancel_fd, POLLOUT, timeout_ms);
        if (result == 0) {
            int socket_error = 0;
            socklen_t length = sizeof(socket_error);

            if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error,
                           &length) != 0) {
                result = -errno;
            } else if (socket_error != 0) {
                result = -socket_error;
            }
        }
    }
    if (result == 0 && is_closing(handle)) {
        result = -ECANCELED;
    }
done:
    end_call(handle);
    return result;
}

static int rfcomm_read(struct lp3_rfcomm_handle *handle, void *buffer,
                       size_t length) {
    int socket_fd;
    int cancel_fd;
    int result = begin_call(handle, &socket_fd, &cancel_fd);

    if (result != 0) {
        return result;
    }
    for (;;) {
        ssize_t received;

        result = wait_for_socket(socket_fd, cancel_fd, POLLIN, -1);
        if (result != 0) {
            break;
        }
        received = recv(socket_fd, buffer, length, 0);
        if (received >= 0) {
            result = (int)received;
            break;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            result = -errno;
            break;
        }
    }
    end_call(handle);
    return result;
}

static int rfcomm_write(struct lp3_rfcomm_handle *handle, const void *buffer,
                        size_t length) {
    int socket_fd;
    int cancel_fd;
    int result = begin_call(handle, &socket_fd, &cancel_fd);

    if (result != 0) {
        return result;
    }
    for (;;) {
        ssize_t written;

        result = wait_for_socket(socket_fd, cancel_fd, POLLOUT, -1);
        if (result != 0) {
            break;
        }
        written = send(socket_fd, buffer, length, MSG_NOSIGNAL);
        if (written >= 0) {
            result = (int)written;
            break;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            result = -errno;
            break;
        }
    }
    end_call(handle);
    return result;
}

static void rfcomm_shutdown(struct lp3_rfcomm_handle *handle) {
    int socket_fd;
    int cancel_fd;
    uint8_t value = 1;

    if (handle == NULL) {
        return;
    }
    pthread_mutex_lock(&handle->lock);
    handle->closing = 1;
    socket_fd = handle->socket_fd;
    cancel_fd = handle->cancel_write_fd;
    pthread_mutex_unlock(&handle->lock);
    /* Wake poll through the cancellation descriptor before shutting down the socket.  Otherwise
     * a reader can observe socket EOF/HUP first and report a spurious successful EOF instead of
     * the cancellation requested by this handle's owner (notably under emulation or load). */
    if (cancel_fd >= 0) {
        ssize_t ignored = write(cancel_fd, &value, sizeof(value));
        (void)ignored;
    }
    if (socket_fd >= 0) {
        shutdown(socket_fd, SHUT_RDWR);
    }
}

static void destroy_handle(struct lp3_rfcomm_handle *handle) {
    int socket_fd;
    int cancel_read_fd;
    int cancel_write_fd;

    if (handle == NULL) {
        return;
    }
    rfcomm_shutdown(handle);
    pthread_mutex_lock(&handle->lock);
    while (handle->active_calls != 0) {
        pthread_cond_wait(&handle->idle, &handle->lock);
    }
    socket_fd = handle->socket_fd;
    cancel_read_fd = handle->cancel_read_fd;
    cancel_write_fd = handle->cancel_write_fd;
    handle->socket_fd = -1;
    handle->cancel_read_fd = -1;
    handle->cancel_write_fd = -1;
    pthread_mutex_unlock(&handle->lock);
    close(socket_fd);
    close(cancel_read_fd);
    close(cancel_write_fd);
    pthread_cond_destroy(&handle->idle);
    pthread_mutex_destroy(&handle->lock);
    free(handle);
}

static struct lp3_rfcomm_handle *handle_from_java(jlong value) {
    return (struct lp3_rfcomm_handle *)(uintptr_t)value;
}

JNIEXPORT jlong JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_rfcommCreate(
    JNIEnv *env, jclass klass, jstring address, jint channel) {
    const char *address_utf8;
    struct lp3_rfcomm_handle *handle;
    (void)klass;

    if (address == NULL) {
        return 0;
    }
    address_utf8 = (*env)->GetStringUTFChars(env, address, NULL);
    if (address_utf8 == NULL) {
        return 0;
    }
    handle = create_handle(address_utf8, channel);
    (*env)->ReleaseStringUTFChars(env, address, address_utf8);
    return (jlong)(uintptr_t)handle;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_rfcommConnect(
    JNIEnv *env, jclass klass, jlong value, jint timeout_ms) {
    (void)env;
    (void)klass;
    if (value == 0 || timeout_ms <= 0 ||
        timeout_ms > LP3_RFCOMM_MAX_TIMEOUT_MS) {
        return -EINVAL;
    }
    return rfcomm_connect(handle_from_java(value), timeout_ms);
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_rfcommRead(
    JNIEnv *env, jclass klass, jlong value, jbyteArray bytes, jint offset,
    jint length) {
    jsize array_length;
    uint8_t *buffer;
    int result;
    (void)klass;

    if (value == 0 || bytes == NULL || offset < 0 || length <= 0 ||
        length > LP3_RFCOMM_MAX_IO) {
        return -EINVAL;
    }
    array_length = (*env)->GetArrayLength(env, bytes);
    if (offset > array_length || length > array_length - offset) {
        return -EINVAL;
    }
    buffer = malloc((size_t)length);
    if (buffer == NULL) {
        return -ENOMEM;
    }
    result = rfcomm_read(handle_from_java(value), buffer, (size_t)length);
    if (result > 0) {
        (*env)->SetByteArrayRegion(env, bytes, offset, result,
                                   (const jbyte *)buffer);
        if ((*env)->ExceptionCheck(env)) {
            result = -EFAULT;
        }
    }
    free(buffer);
    return result;
}

JNIEXPORT jint JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_rfcommWrite(
    JNIEnv *env, jclass klass, jlong value, jbyteArray bytes, jint offset,
    jint length) {
    jsize array_length;
    uint8_t *buffer;
    int result;
    (void)klass;

    if (value == 0 || bytes == NULL || offset < 0 || length <= 0 ||
        length > LP3_RFCOMM_MAX_IO) {
        return -EINVAL;
    }
    array_length = (*env)->GetArrayLength(env, bytes);
    if (offset > array_length || length > array_length - offset) {
        return -EINVAL;
    }
    buffer = malloc((size_t)length);
    if (buffer == NULL) {
        return -ENOMEM;
    }
    (*env)->GetByteArrayRegion(env, bytes, offset, length, (jbyte *)buffer);
    if ((*env)->ExceptionCheck(env)) {
        free(buffer);
        return -EFAULT;
    }
    result = rfcomm_write(handle_from_java(value), buffer, (size_t)length);
    free(buffer);
    return result;
}

JNIEXPORT void JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_rfcommShutdown(
    JNIEnv *env, jclass klass, jlong value) {
    (void)env;
    (void)klass;
    if (value != 0) {
        rfcomm_shutdown(handle_from_java(value));
    }
}

JNIEXPORT void JNICALL
Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_rfcommDestroy(
    JNIEnv *env, jclass klass, jlong value) {
    (void)env;
    (void)klass;
    if (value != 0) {
        destroy_handle(handle_from_java(value));
    }
}
