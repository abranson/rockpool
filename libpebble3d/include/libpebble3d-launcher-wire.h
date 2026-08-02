/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private fixed-size control protocol between the Sailfish session launcher,
 * the unprivileged daemon bootstrap, and the provider proxy.  This is not part
 * of the public platform-provider ABI.
 */

#ifndef LIBPEBBLE3D_LAUNCHER_WIRE_H
#define LIBPEBBLE3D_LAUNCHER_WIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LP3_LAUNCHER_CONTROL_FD 3
#define LP3_LAUNCHER_MAGIC UINT32_C(0x4c50334c)
#define LP3_LAUNCHER_VERSION UINT16_C(1)

enum lp3_launcher_message_type {
    LP3_LAUNCHER_DAEMON_READY = 1,
    LP3_LAUNCHER_START_HOST = 2,
    LP3_LAUNCHER_STOP_HOST = 3,
    LP3_LAUNCHER_HOST_STARTED = 4,
    LP3_LAUNCHER_HOST_STOPPED = 5,
    LP3_LAUNCHER_ERROR = 6,
};

struct lp3_launcher_message_v1 {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    int32_t status;
    int32_t process_id;
};

#if defined(__cplusplus)
static_assert(sizeof(struct lp3_launcher_message_v1) == 16,
              "launcher message layout changed");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct lp3_launcher_message_v1) == 16,
               "launcher message layout changed");
#endif

#ifdef __cplusplus
}
#endif

#endif
