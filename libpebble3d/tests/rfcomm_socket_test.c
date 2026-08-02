/* SPDX-License-Identifier: Apache-2.0 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../native/rfcomm_socket.c"

struct blocked_read {
    struct lp3_rfcomm_handle *handle;
    int result;
};

static void *read_until_cancelled(void *opaque) {
    struct blocked_read *read = opaque;
    uint8_t byte;

    read->result = rfcomm_read(read->handle, &byte, sizeof(byte));
    return NULL;
}

static struct lp3_rfcomm_handle *connected_handle(int *peer) {
    struct lp3_sockaddr_rc remote;
    int sockets[2];

    memset(&remote, 0, sizeof(remote));
    remote.family = AF_BLUETOOTH;
    remote.channel = 1;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    *peer = sockets[1];
    return allocate_handle(sockets[0], &remote);
}

int main(void) {
    struct lp3_bdaddr address;
    struct lp3_rfcomm_handle *handle;
    struct blocked_read blocked;
    pthread_t thread;
    uint8_t data[4];
    int peer;

    assert(parse_bluetooth_address("02:11:22:33:12:34", &address));
    assert(address.bytes[0] == 0x34);
    assert(address.bytes[1] == 0x12);
    assert(address.bytes[2] == 0x33);
    assert(address.bytes[3] == 0x22);
    assert(address.bytes[4] == 0x11);
    assert(address.bytes[5] == 0x02);
    assert(!parse_bluetooth_address("02:11:22:33:12", &address));
    assert(!parse_bluetooth_address("02-11-22-33-12-34", &address));
    assert(!parse_bluetooth_address("02:11:22:33:12:CG", &address));
    assert(create_handle("02:11:22:33:12:34", 0) == NULL);
    assert(create_handle("02:11:22:33:12:34", 31) == NULL);
    assert(create_handle("not-an-address", 1) == NULL);

    handle = connected_handle(&peer);
    assert(handle != NULL);
    assert(handle->remote.family == AF_BLUETOOTH);
    assert(handle->remote.channel == 1);
    assert(write(peer, "test", 4) == 4);
    assert(rfcomm_read(handle, data, sizeof(data)) == 4);
    assert(memcmp(data, "test", 4) == 0);
    assert(rfcomm_write(handle, "pong", 4) == 4);
    assert(read(peer, data, sizeof(data)) == 4);
    assert(memcmp(data, "pong", 4) == 0);
    destroy_handle(handle);
    close(peer);

    handle = connected_handle(&peer);
    assert(handle != NULL);
    blocked.handle = handle;
    blocked.result = 0;
    assert(wait_for_socket(handle->socket_fd, handle->cancel_read_fd,
                           POLLIN, 10) == -ETIMEDOUT);
    assert(pthread_create(&thread, NULL, read_until_cancelled, &blocked) == 0);
    do {
        pthread_mutex_lock(&handle->lock);
        if (handle->active_calls != 0) {
            pthread_mutex_unlock(&handle->lock);
            break;
        }
        pthread_mutex_unlock(&handle->lock);
        sched_yield();
    } while (1);
    rfcomm_shutdown(handle);
    assert(pthread_join(thread, NULL) == 0);
    assert(blocked.result == -ECANCELED);
    assert(rfcomm_read(handle, data, sizeof(data)) == -ECANCELED);
    destroy_handle(handle);
    close(peer);
    return 0;
}
