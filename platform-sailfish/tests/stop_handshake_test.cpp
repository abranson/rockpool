/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * White-box coverage for the proxy/launcher STOP_HOST acknowledgement path.
 */
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../proxy/sailfish_proxy.cpp"

namespace {

struct CapturedProviderStatus {
    unsigned int count;
    uint32_t state;
    uint64_t readyDomains;
    uint64_t degradedDomains;
    uint64_t failedDomains;
    int64_t helperPid;

    CapturedProviderStatus()
        : count(0), state(0), readyDomains(0), degradedDomains(0),
          failedDomains(0), helperPid(0) {}
};

void captureProviderStatus(void *context,
                           const lp3_platform_event_v1 *event) {
    CapturedProviderStatus *captured =
        static_cast<CapturedProviderStatus *>(context);
    assert(captured != NULL);
    assert(event != NULL);
    assert(event->type == LP3_PLATFORM_EVENT_PROVIDER_STATUS);
    assert(event->provider_status != NULL);
    ++captured->count;
    captured->state = event->provider_status->state;
    captured->readyDomains = event->provider_status->ready_domains;
    captured->degradedDomains = event->provider_status->degraded_domains;
    captured->failedDomains = event->provider_status->failed_domains;
    captured->helperPid = event->provider_status->helper_pid;
}

void sendStoppedReply(int fd) {
    lp3_launcher_message_v1 reply;
    ssize_t sent;

    memset(&reply, 0, sizeof(reply));
    reply.magic = LP3_LAUNCHER_MAGIC;
    reply.version = LP3_LAUNCHER_VERSION;
    reply.type = LP3_LAUNCHER_HOST_STOPPED;
    do {
        sent = send(fd, &reply, sizeof(reply), MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    assert(sent == static_cast<ssize_t>(sizeof(reply)));
}

void sendStartedReply(int fd, int dataFd) {
    lp3_launcher_message_v1 reply;
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov;
    struct msghdr message;
    struct cmsghdr *header;
    ssize_t sent;

    memset(&reply, 0, sizeof(reply));
    reply.magic = LP3_LAUNCHER_MAGIC;
    reply.version = LP3_LAUNCHER_VERSION;
    reply.type = LP3_LAUNCHER_HOST_STARTED;
    reply.process_id = 42;
    memset(control, 0, sizeof(control));
    memset(&message, 0, sizeof(message));
    iov.iov_base = &reply;
    iov.iov_len = sizeof(reply);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    header = CMSG_FIRSTHDR(&message);
    assert(header != NULL);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &dataFd, sizeof(dataFd));
    do {
        sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    assert(sent == static_cast<ssize_t>(sizeof(reply)));
}

void testStopReplyIsConsumedWhileStopping() {
    int pair[2];
    SailfishInstance instance;
    lp3_launcher_message_v1 reply;
    int receivedFd = -1;

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    instance.stopping.store(true);
    sendStoppedReply(pair[1]);

    assert(receiveLauncherReply(&instance, pair[0], &reply, &receivedFd,
                                100, true));
    assert(receivedFd == -1);
    assert(reply.type == LP3_LAUNCHER_HOST_STOPPED);
    assert(reply.status == 0);
    assert(reply.process_id == 0);
    close(pair[0]);
    close(pair[1]);
}

void testRegularReplyWaitRemainsInterruptible() {
    int pair[2];
    SailfishInstance instance;
    lp3_launcher_message_v1 reply;
    int receivedFd = -1;

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    instance.stopping.store(true);
    sendStoppedReply(pair[1]);

    assert(!receiveLauncherReply(&instance, pair[0], &reply, &receivedFd,
                                 100, false));
    instance.stopping.store(false);
    assert(receiveLauncherReply(&instance, pair[0], &reply, &receivedFd,
                                100, false));
    assert(receivedFd == -1);
    assert(reply.type == LP3_LAUNCHER_HOST_STOPPED);
    close(pair[0]);
    close(pair[1]);
}

void testExpectedStartSkipsLateStop() {
    int control[2];
    int data[2];
    SailfishInstance instance;
    lp3_launcher_message_v1 reply;
    int receivedFd = -1;

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, data) == 0);
    sendStoppedReply(control[1]);
    sendStartedReply(control[1], data[0]);
    assert(receiveExpectedLauncherReply(
        &instance, control[0], LP3_LAUNCHER_HOST_STARTED,
        &reply, &receivedFd, 100, false));
    assert(reply.type == LP3_LAUNCHER_HOST_STARTED);
    assert(receivedFd >= 0);
    close(receivedFd);
    close(data[0]);
    close(data[1]);
    close(control[0]);
    close(control[1]);
}

void testExpectedStopClosesLateStartedDescriptor() {
    int control[2];
    int data[2];
    SailfishInstance instance;
    lp3_launcher_message_v1 reply;
    int receivedFd = -1;

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, data) == 0);
    instance.stopping.store(true);
    sendStartedReply(control[1], data[0]);
    sendStoppedReply(control[1]);
    assert(receiveExpectedLauncherReply(
        &instance, control[0], LP3_LAUNCHER_HOST_STOPPED,
        &reply, &receivedFd, 100, true));
    assert(reply.type == LP3_LAUNCHER_HOST_STOPPED);
    assert(receivedFd == -1);
    close(data[0]);
    close(data[1]);
    close(control[0]);
    close(control[1]);
}

void testHealthReadyLossPublishesAuthorityBarrier() {
    SailfishInstance instance;
    CapturedProviderStatus captured;
    lp3wire::HealthState health;
    lp3wire::Frame frame;

    instance.event = captureProviderStatus;
    instance.eventContext = &captured;
    instance.readyDomains = kSupportedDomains;
    instance.degradedDomains = 0;
    instance.failedDomains = 0;

    health.readyDomains = kSupportedDomains & ~lp3wire::DomainMessaging;
    health.degradedDomains = lp3wire::DomainMessaging;
    health.failedDomains = 0;
    frame.type = lp3wire::Health;
    frame.requestId = 0;
    assert(lp3wire::encodeHealth(health, &frame.payload));
    assert(dispatchFrame(&instance, frame));
    assert(captured.count == 1);
    assert(captured.state == LP3_PLATFORM_PROVIDER_DEGRADED);
    assert(captured.readyDomains == 0);
    assert(captured.degradedDomains == lp3wire::DomainMessaging);
    assert(captured.failedDomains == 0);
    assert(captured.helperPid == 0);

    health.readyDomains = kSupportedDomains;
    health.degradedDomains = 0;
    frame.payload.clear();
    assert(lp3wire::encodeHealth(health, &frame.payload));
    assert(dispatchFrame(&instance, frame));
    assert(captured.count == 1);
}

} // namespace

int main() {
    testStopReplyIsConsumedWhileStopping();
    testRegularReplyWaitRemainsInterruptible();
    testExpectedStartSkipsLateStop();
    testExpectedStopClosesLateStartedDescriptor();
    testHealthReadyLossPublishesAuthorityBarrier();
    return 0;
}
