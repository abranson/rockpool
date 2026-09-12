/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The sole Qt-owning process in the Sailfish platform provider.  It accepts
 * only the inherited private SOCK_SEQPACKET peer on descriptor 3.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <MDConfItem>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>

#include <deque>
#include <string>
#include <vector>

#include "libpebble3d-launcher-wire.h"
#include "libpebble3d-platform.h"
#include "calendarmonitor.h"
#include "callmonitor.h"
#include "contactmonitor.h"
#include "locationmonitor.h"
#include "mainvolumemonitor.h"
#include "notificationmonitor.h"
#include "pebblebondremover.h"
#include "wire.h"

#ifndef LP3_PLATFORM_BUILD_ID
#define LP3_PLATFORM_BUILD_ID "sailfish-provider-v1"
#endif

namespace {

const int kSocketFd = 3;
const char kExpectedBuildId[] = LP3_PLATFORM_BUILD_ID;
const char kPrivilegedGroup[] = "privileged";
const char kHostPath[] =
    "/usr/libexec/libpebble3d/libpebble3d-platform-sailfish-host";

bool helperFailure(const char *stage) {
    openlog("libpebble3d-platform-host", LOG_CONS | LOG_PID, LOG_USER);
    syslog(LOG_ERR, "%s failed", stage);
    closelog();
    return false;
}

bool parseParentPid(pid_t *parent) {
    const char *value = getenv("LP3_PARENT_PID");
    char *end = NULL;
    long parsed;
    if (value == NULL || *value == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == NULL || *end != '\0' || parsed <= 1 ||
        parsed > INT_MAX) {
        return false;
    }
    *parent = static_cast<pid_t>(parsed);
    return true;
}

bool packageOwned(const char *path, mode_t requiredMode, gid_t requiredGroup) {
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        info.st_gid != requiredGroup || (info.st_mode & 07777) != requiredMode) {
        return false;
    }
    return true;
}

bool validateOwnExecutable(gid_t privilegedGroup) {
    char path[PATH_MAX];
    struct stat linkInfo;
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0 || length >= static_cast<ssize_t>(sizeof(path) - 1)) {
        return false;
    }
    path[length] = '\0';
    return strcmp(path, kHostPath) == 0 && lstat(kHostPath, &linkInfo) == 0 &&
        !S_ISLNK(linkInfo.st_mode) && packageOwned(path, 0750, privilegedGroup);
}

bool validateSocket(pid_t expectedParent, gid_t privilegedGroup) {
    int type = 0;
    socklen_t typeLength = sizeof(type);
    struct ucred credentials;
    socklen_t credentialLength = sizeof(credentials);
    if (getsockopt(kSocketFd, SOL_SOCKET, SO_TYPE, &type, &typeLength) != 0 ||
        type != SOCK_SEQPACKET ||
        getsockopt(kSocketFd, SOL_SOCKET, SO_PEERCRED, &credentials, &credentialLength) != 0) {
        return false;
    }
    return credentials.pid == expectedParent && credentials.uid == getuid() &&
        credentials.gid == privilegedGroup;
}

bool sanitizeStandardDescriptors() {
    int input = open("/dev/null", O_RDONLY);
    int output = open("/dev/null", O_WRONLY);
    bool valid = input >= 0 && output >= 0 && dup2(input, STDIN_FILENO) >= 0 &&
        dup2(output, STDOUT_FILENO) >= 0 && dup2(output, STDERR_FILENO) >= 0;

    if (input > STDERR_FILENO) {
        close(input);
    }
    if (output > STDERR_FILENO) {
        close(output);
    }
    return valid;
}

void closeUnrelatedDescriptors() {
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 4U, UINT_MAX, 0U) == 0) {
        return;
    }
#endif
    const long maximum = sysconf(_SC_OPEN_MAX);
    for (int fd = 4; fd < maximum; ++fd) {
        close(fd);
    }
}

bool harden() {
    pid_t expectedParent;
    struct group *privileged;
    struct rlimit coreLimit;
    gid_t realGroup;
    gid_t effectiveGroup;
    gid_t savedGroup;
    struct passwd *password;
    char runtimeDirectory[64];
    char sessionBus[128];
    const char *buildId = getenv("LP3_PLATFORM_BUILD_ID");

    if (!parseParentPid(&expectedParent) || getppid() != expectedParent) {
        return helperFailure("launcher parent validation");
    }
    if (buildId == NULL || strcmp(buildId, kExpectedBuildId) != 0) {
        return helperFailure("package build validation");
    }
    if (prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) != 0 ||
        getppid() != expectedParent) {
        return helperFailure("launcher lifetime validation");
    }
    password = getpwuid(getuid());
    if (password == NULL || password->pw_dir == NULL || password->pw_name == NULL ||
        password->pw_dir[0] != '/' ||
        snprintf(runtimeDirectory, sizeof(runtimeDirectory), "/run/user/%lu",
                 static_cast<unsigned long>(getuid())) >=
            static_cast<int>(sizeof(runtimeDirectory)) ||
        snprintf(sessionBus, sizeof(sessionBus),
                 "unix:path=/run/user/%lu/dbus/user_bus_socket",
                 static_cast<unsigned long>(getuid())) >=
            static_cast<int>(sizeof(sessionBus))) {
        return helperFailure("session identity validation");
    }
    privileged = getgrnam(kPrivilegedGroup);
    if (privileged == NULL) {
        return helperFailure("privileged group lookup");
    }
    if (getresgid(&realGroup, &effectiveGroup, &savedGroup) != 0) {
        return helperFailure("inherited group query");
    }
    if (realGroup != privileged->gr_gid) {
        return helperFailure("real group inheritance");
    }
    if (effectiveGroup != privileged->gr_gid) {
        return helperFailure("effective group inheritance");
    }
    if (savedGroup != privileged->gr_gid) {
        return helperFailure("saved group inheritance");
    }
    if (!validateOwnExecutable(privileged->gr_gid)) {
        return helperFailure("host package validation");
    }
    if (!validateSocket(expectedParent, privileged->gr_gid)) {
        return helperFailure("launcher socket validation");
    }
    /*
     * The setgid launcher supplied and normalized all privileged group IDs
     * immediately before executing this exact host.  Reassert that invariant
     * before Qt starts; the host itself remains a non-setgid
     * root:privileged 0750 executable which the session user cannot invoke.
     */
    if (clearenv() != 0 || setenv("PATH", "/usr/bin:/bin", 1) != 0 ||
        setenv("LANG", "C", 1) != 0 || setenv("LC_ALL", "C", 1) != 0 ||
        setenv("HOME", password->pw_dir, 1) != 0 ||
        setenv("USER", password->pw_name, 1) != 0 ||
        setenv("LOGNAME", password->pw_name, 1) != 0 ||
        setenv("XDG_RUNTIME_DIR", runtimeDirectory, 1) != 0 ||
        setenv("DBUS_SESSION_BUS_ADDRESS", sessionBus, 1) != 0) {
        return helperFailure("environment sanitization");
    }
    if (!sanitizeStandardDescriptors()) {
        return helperFailure("standard descriptor sanitization");
    }
    closeUnrelatedDescriptors();
    coreLimit.rlim_cur = 0;
    coreLimit.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &coreLimit) != 0) {
        return helperFailure("core-dump hardening");
    }
    if (setresgid(privileged->gr_gid, privileged->gr_gid,
                  privileged->gr_gid) != 0) {
        return helperFailure("privileged group normalization");
    }
    if (fcntl(kSocketFd, F_SETFD, FD_CLOEXEC) != 0) {
        return helperFailure("control descriptor hardening");
    }
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 ||
        getppid() != expectedParent ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return helperFailure("process hardening");
    }
    return true;
}

class HelperConnection : public QObject {
public:
    explicit HelperConnection(int fd, QObject *parent = 0)
        : QObject(parent), m_fd(fd), m_phase(AwaitHello),
          m_readNotifier(fd, QSocketNotifier::Read, this),
          m_writeNotifier(fd, QSocketNotifier::Write, this),
          m_timeFormat(QStringLiteral("/sailfish/i18n/lc_timeformat24h"), this),
          m_notifications(
              [this](const NotificationMonitor::Notification &notification) {
                  emitNotification(lp3wire::NotificationPosted, notification);
              },
              [this](const NotificationMonitor::Notification &notification) {
                  emitNotification(lp3wire::NotificationClosed, notification);
              },
              [this](bool ready) { updateNotificationHealth(ready); },
              [this](bool ready) { updateMessagingHealth(ready); },
              this),
          m_notificationsReady(false), m_messagingReady(false),
          m_calls(
              [this](const CallMonitor::Call &call) { emitCall(call); },
              [this](bool ready) { updateCallHealth(ready); },
              this),
          m_callsReady(false),
          m_media(
              [this](const MainVolumeMonitor::State &state) {
                  updateMediaState(state);
              },
              [this](bool ready) { updateMediaHealth(ready); },
              this),
          m_mediaReady(false), m_mediaStatePending(false),
          m_location(
              [this](quint64 requestId, int32_t status,
                     const LocationMonitor::Fix &fix) {
                  completeLocation(requestId, status, fix);
              },
              [this](bool ready) { updateLocationHealth(ready); },
              this),
          m_locationReady(false),
          m_calendar(
              [this](quint64 requestId, int32_t status,
                     const QByteArray &payload) {
                  completeCalendar(requestId, status, payload);
              },
              [this]() { emitCalendarChanged(); },
              [this](bool ready) { updateCalendarHealth(ready); },
              this),
          m_calendarReady(false),
          m_contacts(
              [this](quint64 requestId, int32_t status,
                     const QByteArray &payload) {
                  completeContact(requestId, status, payload);
              },
              [this]() { emitContactChanged(); },
              [this](bool ready) { updateContactHealth(ready); },
              this),
          m_contactsReady(false) {
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            QTimer::singleShot(0, QCoreApplication::instance(),
                               &QCoreApplication::quit);
            return;
        }
        m_writeNotifier.setEnabled(false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        QObject::connect(
            &m_readNotifier,
            &QSocketNotifier::activated,
            this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { onReadable(); });
        QObject::connect(
            &m_writeNotifier,
            &QSocketNotifier::activated,
            this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { onWritable(); });
#else
        QObject::connect(
            &m_readNotifier,
            &QSocketNotifier::activated,
            this,
            [this](int) { onReadable(); });
        QObject::connect(
            &m_writeNotifier,
            &QSocketNotifier::activated,
            this,
            [this](int) { onWritable(); });
#endif
        QObject::connect(&m_timeFormat, &MDConfItem::valueChanged, this, [this]() {
            emitTimeChanged();
        });
        m_changeTimer.setInterval(5000);
        QObject::connect(&m_changeTimer, &QTimer::timeout, this, [this]() {
            checkTimeChanged();
        });
        m_lastWallMs = QDateTime::currentMSecsSinceEpoch();
        m_elapsed.start();
        m_lastOffsetSeconds = QDateTime::currentDateTime().offsetFromUtc();
        m_changeTimer.start();
    }

private:
    static const int kMaximumPending = 64;
    static const int kMaximumOutgoing = 64;

    void failClosed() {
        m_readNotifier.setEnabled(false);
        m_writeNotifier.setEnabled(false);
        QCoreApplication::quit();
    }

    lp3wire::TimeState timeState() const {
        const QDateTime local = QDateTime::currentDateTime();
        lp3wire::TimeState state;
        state.utcOffsetSeconds = local.offsetFromUtc();
        state.unixMs = local.toMSecsSinceEpoch();
        state.is24Hour =
            m_timeFormat.value(QStringLiteral("24")).toString() == QStringLiteral("12") ? 0 : 1;
        return state;
    }

    bool queueFrame(uint16_t type, uint64_t requestId,
                    const uint8_t *payload, size_t payloadSize) {
        std::vector<uint8_t> frame;
        if (m_outgoing.size() >= kMaximumOutgoing ||
            !lp3wire::encodeFrame(type, requestId, payload, payloadSize, &frame)) {
            return false;
        }
        m_outgoing.push_back(frame);
        return flushOutgoing();
    }

    bool queueTimeChanged(const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> frame;
        if (!lp3wire::encodeFrame(lp3wire::Event, 0, &payload[0], payload.size(),
                                  &frame)) {
            return false;
        }
        for (std::deque<std::vector<uint8_t> >::iterator it = m_outgoing.begin();
             it != m_outgoing.end(); ++it) {
            if (lp3wire::isEventFrame(*it, lp3wire::TimeChanged)) {
                *it = frame;
                return flushOutgoing();
            }
        }
        // A time-change event is level-triggered: a later event or TimeGet
        // returns the current state, so dropping it is safer than blocking or
        // restarting when replies already occupy the bounded queue.
        if (m_outgoing.size() >= kMaximumOutgoing) {
            return true;
        }
        m_outgoing.push_back(frame);
        return flushOutgoing();
    }

    bool queueMediaChanged(const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> frame;
        if (!lp3wire::encodeFrame(lp3wire::Event, 0, &payload[0], payload.size(),
                                  &frame)) {
            return false;
        }
        for (std::deque<std::vector<uint8_t> >::iterator it = m_outgoing.begin();
             it != m_outgoing.end(); ++it) {
            if (lp3wire::isEventFrame(*it, lp3wire::MediaVolumeChanged)) {
                *it = frame;
                return flushOutgoing();
            }
        }
        // System volume is level-triggered.  Keep replies and health bounded;
        // a later StepsUpdated signal or command result supplies fresh state.
        if (m_outgoing.size() >= kMaximumOutgoing) {
            return true;
        }
        m_outgoing.push_back(frame);
        return flushOutgoing();
    }

    bool queueCalendarChanged(const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> frame;
        if (!lp3wire::encodeFrame(lp3wire::Event, 0, &payload[0], payload.size(),
                                  &frame)) {
            return false;
        }
        for (std::deque<std::vector<uint8_t> >::iterator it = m_outgoing.begin();
             it != m_outgoing.end(); ++it) {
            if (lp3wire::isEventFrame(*it, lp3wire::CalendarChanged)) {
                return true;
            }
        }
        if (m_outgoing.size() >= kMaximumOutgoing) {
            return true;
        }
        m_outgoing.push_back(frame);
        return flushOutgoing();
    }

    bool queueContactChanged(const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> frame;
        if (!lp3wire::encodeFrame(lp3wire::Event, 0, &payload[0], payload.size(),
                                  &frame)) {
            return false;
        }
        for (std::deque<std::vector<uint8_t> >::iterator it = m_outgoing.begin();
             it != m_outgoing.end(); ++it) {
            if (lp3wire::isEventFrame(*it, lp3wire::ContactChanged)) {
                return true;
            }
        }
        if (m_outgoing.size() >= kMaximumOutgoing) return true;
        m_outgoing.push_back(frame);
        return flushOutgoing();
    }

    bool queueHealth() {
        lp3wire::HealthState health;
        health.readyDomains = lp3wire::DomainTime |
            (m_notificationsReady ? lp3wire::DomainNotifications : 0) |
            (m_messagingReady ? lp3wire::DomainMessaging : 0) |
            (m_callsReady ? lp3wire::DomainCalls : 0) |
            (m_mediaReady ? lp3wire::DomainMedia : 0) |
            (m_calendarReady ? lp3wire::DomainCalendar : 0) |
            (m_contactsReady ? lp3wire::DomainContacts : 0) |
            (m_locationReady ? lp3wire::DomainLocation : 0);
        health.degradedDomains =
            (m_notificationsReady ? 0 : lp3wire::DomainNotifications) |
            (m_messagingReady ? 0 : lp3wire::DomainMessaging) |
            (m_callsReady ? 0 : lp3wire::DomainCalls) |
            (m_mediaReady ? 0 : lp3wire::DomainMedia) |
            (m_calendarReady ? 0 : lp3wire::DomainCalendar) |
            (m_contactsReady ? 0 : lp3wire::DomainContacts) |
            (m_locationReady ? 0 : lp3wire::DomainLocation);
        health.failedDomains = 0;
        std::vector<uint8_t> payload;
        return lp3wire::encodeHealth(health, &payload) &&
            queueFrame(lp3wire::Health, 0, &payload[0], payload.size());
    }

    void updateNotificationHealth(bool ready) {
        if (m_notificationsReady == ready) {
            return;
        }
        m_notificationsReady = ready;
        if (m_phase == Active && !queueHealth()) {
            failClosed();
        }
    }

    void updateMessagingHealth(bool ready) {
        if (m_messagingReady == ready) {
            return;
        }
        m_messagingReady = ready;
        if (m_phase == Active && !queueHealth()) {
            failClosed();
        }
    }

    void updateCallHealth(bool ready) {
        if (m_callsReady == ready) {
            return;
        }
        m_callsReady = ready;
        if (m_phase == Active && !queueHealth()) {
            failClosed();
        }
    }

    void updateMediaHealth(bool ready) {
        if (m_mediaReady == ready) {
            return;
        }
        m_mediaReady = ready;
        if (m_phase == Active && !queueHealth()) {
            failClosed();
        }
    }

    void updateLocationHealth(bool ready) {
        if (m_locationReady == ready) {
            return;
        }
        m_locationReady = ready;
        if (m_phase == Active && !queueHealth()) {
            failClosed();
        }
    }

    void updateCalendarHealth(bool ready) {
        if (m_calendarReady == ready) {
            return;
        }
        m_calendarReady = ready;
        if (m_phase == Active && !queueHealth()) {
            failClosed();
        }
    }

    void updateContactHealth(bool ready) {
        if (m_contactsReady == ready) return;
        m_contactsReady = ready;
        if (m_phase == Active && !queueHealth()) failClosed();
    }

    void updateMediaState(const MainVolumeMonitor::State &state) {
        m_mediaState = state;
        m_mediaStatePending = true;
        if (m_phase == Active) {
            emitMediaState();
        }
    }

    void emitMediaState() {
        if (m_phase != Active || !m_mediaStatePending ||
            m_mediaState.stepCount < 2 ||
            m_mediaState.currentStep >= m_mediaState.stepCount) {
            return;
        }
        lp3wire::MediaVolumeData data;
        data.flags = lp3wire::MediaSystemVolume;
        data.volumePercent = static_cast<int32_t>(
            static_cast<uint64_t>(m_mediaState.currentStep) * 100 /
            (m_mediaState.stepCount - 1));
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeMediaVolumeChanged(data, &payload) ||
            !queueMediaChanged(payload)) {
            failClosed();
            return;
        }
        m_mediaStatePending = false;
    }

    static std::string boundedUtf8(const QString &value, size_t maximum) {
        QByteArray bytes = value.toUtf8();
        if (bytes.size() > static_cast<int>(maximum)) {
            bytes.truncate(static_cast<int>(maximum));
            while (!bytes.isEmpty() && !lp3wire::validUtf8(std::string(
                       bytes.constData(), static_cast<size_t>(bytes.size())))) {
                bytes.chop(1);
            }
        }
        if (bytes.isEmpty()) {
            return std::string();
        }
        return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
    }

    void emitNotification(
        uint16_t eventType,
        const NotificationMonitor::Notification &notification) {
        if (m_phase != Active) {
            return;
        }
        lp3wire::NotificationData data;
        data.flags = notification.flags;
        data.timestampMs = notification.timestampMs;
        data.closeReason = notification.closeReason;
        data.id = boundedUtf8(notification.id, lp3wire::kNotificationIdMax);
        data.replacesId = boundedUtf8(
            notification.replacesId, lp3wire::kNotificationIdMax);
        data.applicationId = boundedUtf8(
            notification.applicationId,
            lp3wire::kNotificationApplicationIdMax);
        data.applicationName = boundedUtf8(
            notification.applicationName,
            lp3wire::kNotificationApplicationNameMax);
        data.title = boundedUtf8(
            notification.title, lp3wire::kNotificationTitleMax);
        data.body = boundedUtf8(
            notification.body, lp3wire::kNotificationBodyMax);
        data.category = boundedUtf8(
            notification.category, lp3wire::kNotificationCategoryMax);
        data.iconName = boundedUtf8(
            notification.iconName, lp3wire::kNotificationIconNameMax);
        data.image.assign(notification.image.constData(), notification.image.constData() + notification.image.size());
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeNotificationEvent(eventType, data, &payload) ||
            !queueFrame(lp3wire::Event, 0, &payload[0], payload.size())) {
            failClosed();
        }
    }

    void emitCall(const CallMonitor::Call &call) {
        if (m_phase != Active) {
            return;
        }
        lp3wire::CallData data;
        data.state = call.state;
        data.id = boundedUtf8(call.id, lp3wire::kCallIdMax);
        data.name = call.state == lp3wire::CallEnded ? std::string() :
            boundedUtf8(call.name, lp3wire::kCallNameMax);
        data.number = call.state == lp3wire::CallEnded ? std::string() :
            boundedUtf8(call.number, lp3wire::kCallNumberMax);
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeCallChanged(data, &payload) ||
            !queueFrame(lp3wire::Event, 0, &payload[0], payload.size())) {
            failClosed();
        }
    }

    bool flushOutgoing() {
        while (!m_outgoing.empty()) {
            const lp3wire::IoResult result =
                lp3wire::sendPacket(m_fd, m_outgoing.front(), true);
            if (result == lp3wire::IoWouldBlock) {
                m_writeNotifier.setEnabled(true);
                return true;
            }
            if (result != lp3wire::IoFrame) {
                return false;
            }
            m_outgoing.pop_front();
        }
        m_writeNotifier.setEnabled(false);
        return true;
    }

    void onWritable() {
        m_writeNotifier.setEnabled(false);
        if (!flushOutgoing()) {
            failClosed();
        }
    }

    void completeTime(uint64_t requestId) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeTimeReply(LP3_PLATFORM_OK, timeState(), &payload) ||
            !queueFrame(lp3wire::Complete, requestId, &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeNotification(
        uint64_t requestId,
        const lp3wire::NotificationCommandData &command) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        const int32_t status = m_notifications.command(
            command.command, QString::fromUtf8(
                command.id.data(), static_cast<int>(command.id.size())));
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeStatusReply(
                lp3wire::NotificationCommand,
                static_cast<uint32_t>(status), &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeMessageReply(
        uint64_t requestId, const lp3wire::MessageReplyData &reply) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        const int32_t status = m_notifications.reply(
            QString::fromUtf8(reply.notificationId.data(),
                              static_cast<int>(reply.notificationId.size())),
            QString::fromUtf8(reply.text.data(),
                              static_cast<int>(reply.text.size())));
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeStatusReply(
                lp3wire::MessageReply, static_cast<uint32_t>(status), &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeMessageSend(
        uint64_t requestId, const lp3wire::MessageSendData &message) {
        if (!m_pending.remove(requestId)) return;
        const int32_t status = m_notifications.send(
            QString::fromUtf8(message.accountId.data(),
                              static_cast<int>(message.accountId.size())),
            QString::fromUtf8(message.recipient.data(),
                              static_cast<int>(message.recipient.size())),
            QString::fromUtf8(message.text.data(),
                              static_cast<int>(message.text.size())));
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeStatusReply(
                lp3wire::MessageSend, static_cast<uint32_t>(status), &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeCall(uint64_t requestId,
                      const lp3wire::CallCommandData &command) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        const int32_t status = m_calls.command(
            command.command, QString::fromUtf8(
                command.id.data(), static_cast<int>(command.id.size())));
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeStatusReply(
                lp3wire::CallCommand, static_cast<uint32_t>(status), &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeMedia(uint64_t requestId,
                       const lp3wire::MediaCommandData &command) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        const int32_t status = m_media.command(command.command);
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeStatusReply(
                lp3wire::MediaCommand, static_cast<uint32_t>(status), &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completePebbleBondRemove(
        uint64_t requestId,
        const lp3wire::PebbleBondRemoveData &request) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        const int status = sailfishplatform::removePebbleBond(request);
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeStatusReply(
                lp3wire::PebbleBondRemove,
                static_cast<uint32_t>(status), &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeLocation(uint64_t requestId, int32_t status,
                          const LocationMonitor::Fix &fix) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        lp3wire::LocationData location = {};
        if (status == LP3_PLATFORM_OK) {
            location.latitudeE7 = fix.latitudeE7;
            location.longitudeE7 = fix.longitudeE7;
            location.accuracyM = fix.accuracyM;
            location.timestampMs = fix.timestampMs;
        }
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeLocationReply(
                static_cast<uint32_t>(status), location, &payload) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeCalendar(uint64_t requestId, int32_t status,
                          const QByteArray &bytes) {
        if (!m_pending.remove(requestId)) {
            return;
        }
        m_calendarPending.remove(requestId);
        if (bytes.isEmpty()) {
            failClosed();
            return;
        }
        const uint8_t *begin =
            reinterpret_cast<const uint8_t *>(bytes.constData());
        const std::vector<uint8_t> payload(begin, begin + bytes.size());
        uint32_t decodedStatus = 0;
        lp3wire::CalendarReplyData reply;
        if (!lp3wire::decodeCalendarReply(payload, &decodedStatus, &reply) ||
            decodedStatus != static_cast<uint32_t>(status) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    void completeContact(uint64_t requestId, int32_t status,
                         const QByteArray &bytes) {
        if (!m_pending.remove(requestId)) return;
        m_contactPending.remove(requestId);
        if (bytes.isEmpty()) {
            failClosed();
            return;
        }
        const uint8_t *begin =
            reinterpret_cast<const uint8_t *>(bytes.constData());
        const std::vector<uint8_t> payload(begin, begin + bytes.size());
        uint32_t decodedStatus = 0;
        lp3wire::ContactReplyData reply;
        if (!lp3wire::decodeContactReply(payload, &decodedStatus, &reply) ||
            decodedStatus != static_cast<uint32_t>(status) ||
            !queueFrame(lp3wire::Complete, requestId,
                        &payload[0], payload.size())) {
            failClosed();
        }
    }

    bool completeBusy(uint64_t requestId, uint16_t operation,
                      uint32_t calendarKind =
                          lp3wire::CalendarQueryCalendars,
                      uint32_t contactKind = lp3wire::ContactQueryList) {
        std::vector<uint8_t> payload;
        if (operation == lp3wire::TimeGet) {
            lp3wire::TimeState empty;
            memset(&empty, 0, sizeof(empty));
            return lp3wire::encodeTimeReply(
                       LP3_PLATFORM_BUSY, empty, &payload) &&
                queueFrame(lp3wire::Complete, requestId,
                           &payload[0], payload.size());
        }
        if (operation == lp3wire::LocationQuery) {
            lp3wire::LocationData empty = {};
            return lp3wire::encodeLocationReply(
                       LP3_PLATFORM_BUSY, empty, &payload) &&
                queueFrame(lp3wire::Complete, requestId,
                           &payload[0], payload.size());
        }
        if (operation == lp3wire::CalendarQuery) {
            lp3wire::CalendarReplyData reply;
            reply.kind = calendarKind;
            reply.nextOffset = 0;
            return lp3wire::encodeCalendarReply(
                       LP3_PLATFORM_BUSY, reply, &payload) &&
                queueFrame(lp3wire::Complete, requestId,
                           &payload[0], payload.size());
        }
        if (operation == lp3wire::ContactQuery) {
            lp3wire::ContactReplyData reply;
            reply.kind = contactKind;
            reply.nextOffset = 0;
            return lp3wire::encodeContactReply(
                       LP3_PLATFORM_BUSY, reply, &payload) &&
                queueFrame(lp3wire::Complete, requestId,
                           &payload[0], payload.size());
        }
        return (operation == lp3wire::NotificationCommand ||
                operation == lp3wire::MessageReply ||
                operation == lp3wire::MessageSend ||
                operation == lp3wire::CallCommand ||
                operation == lp3wire::MediaCommand ||
                operation == lp3wire::PebbleBondRemove) &&
            lp3wire::encodeStatusReply(
                operation, LP3_PLATFORM_BUSY, &payload) &&
            queueFrame(lp3wire::Complete, requestId,
                       &payload[0], payload.size());
    }

    bool handleFrame(const lp3wire::Frame &frame) {
        if (m_phase == AwaitHello) {
            const uint8_t *buildId =
                reinterpret_cast<const uint8_t *>(kExpectedBuildId);
            if (frame.type != lp3wire::Hello || frame.requestId != 0 ||
                frame.payload.size() != strlen(kExpectedBuildId) ||
                memcmp(&frame.payload[0], kExpectedBuildId,
                       strlen(kExpectedBuildId)) != 0 ||
                !queueFrame(lp3wire::HelloAck, 0, buildId,
                            strlen(kExpectedBuildId))) {
                return false;
            }
            m_phase = AwaitReady;
            return true;
        }
        if (m_phase == AwaitReady) {
            if (frame.type != lp3wire::Ready || frame.requestId != 0 ||
                !frame.payload.empty()) {
                return false;
            }
            m_notificationsReady = m_notifications.start();
            m_callsReady = m_calls.start();
            m_mediaReady = m_media.start();
            m_locationReady = m_location.start();
            m_calendarReady = m_calendar.start();
            m_contactsReady = m_contacts.start();
            m_phase = Active;
            emitMediaState();
            return queueHealth();
        }
        if (frame.type == lp3wire::Request) {
            if (frame.requestId == 0 || frame.payload.size() < 4 ||
                m_pending.contains(frame.requestId)) {
                return false;
            }
            const uint16_t operation = lp3wire::get16(&frame.payload[0]);
            lp3wire::NotificationCommandData notificationCommand;
            lp3wire::MessageReplyData messageReply;
            lp3wire::MessageSendData messageSend;
            lp3wire::CallCommandData callCommand;
            lp3wire::MediaCommandData mediaCommand = {};
            lp3wire::LocationQueryData locationQuery = {};
            lp3wire::CalendarQueryData calendarQuery = {};
            lp3wire::ContactQueryData contactQuery = {};
            lp3wire::PebbleBondRemoveData pebbleBondRemove = {};
            if ((operation == lp3wire::TimeGet &&
                 !lp3wire::decodeTimeGet(frame.payload)) ||
                (operation == lp3wire::NotificationCommand &&
                 !lp3wire::decodeNotificationCommand(
                     frame.payload, &notificationCommand)) ||
                (operation == lp3wire::MessageReply &&
                 !lp3wire::decodeMessageReply(frame.payload, &messageReply)) ||
                (operation == lp3wire::MessageSend &&
                 !lp3wire::decodeMessageSend(frame.payload, &messageSend)) ||
                (operation == lp3wire::CallCommand &&
                 !lp3wire::decodeCallCommand(frame.payload, &callCommand)) ||
                (operation == lp3wire::MediaCommand &&
                 !lp3wire::decodeMediaCommand(frame.payload, &mediaCommand)) ||
                (operation == lp3wire::LocationQuery &&
                 !lp3wire::decodeLocationQuery(frame.payload, &locationQuery)) ||
                (operation == lp3wire::CalendarQuery &&
                 !lp3wire::decodeCalendarQuery(frame.payload, &calendarQuery)) ||
                (operation == lp3wire::ContactQuery &&
                 !lp3wire::decodeContactQuery(frame.payload, &contactQuery)) ||
                (operation == lp3wire::PebbleBondRemove &&
                 !lp3wire::decodePebbleBondRemove(
                     frame.payload, &pebbleBondRemove)) ||
                (operation != lp3wire::TimeGet &&
                 operation != lp3wire::NotificationCommand &&
                 operation != lp3wire::MessageReply &&
                 operation != lp3wire::MessageSend &&
                 operation != lp3wire::CallCommand &&
                 operation != lp3wire::MediaCommand &&
                 operation != lp3wire::LocationQuery &&
                 operation != lp3wire::CalendarQuery &&
                 operation != lp3wire::ContactQuery &&
                 operation != lp3wire::PebbleBondRemove)) {
                return false;
            }
            if (m_pending.size() >= kMaximumPending) {
                return completeBusy(
                    frame.requestId, operation,
                    operation == lp3wire::CalendarQuery ? calendarQuery.kind :
                        lp3wire::CalendarQueryCalendars,
                    operation == lp3wire::ContactQuery ? contactQuery.kind :
                        lp3wire::ContactQueryList);
            }
            m_pending.insert(frame.requestId);
            if (operation == lp3wire::TimeGet) {
                QTimer::singleShot(0, this, [this, frame]() {
                    completeTime(frame.requestId);
                });
            } else if (operation == lp3wire::NotificationCommand) {
                QTimer::singleShot(
                    0, this,
                    [this, frame, notificationCommand]() {
                        completeNotification(
                            frame.requestId, notificationCommand);
                    });
            } else if (operation == lp3wire::MessageReply) {
                QTimer::singleShot(
                    0, this,
                    [this, frame, messageReply]() {
                        completeMessageReply(frame.requestId, messageReply);
                    });
            } else if (operation == lp3wire::MessageSend) {
                QTimer::singleShot(
                    0, this,
                    [this, frame, messageSend]() {
                        completeMessageSend(frame.requestId, messageSend);
                    });
            } else if (operation == lp3wire::CallCommand) {
                QTimer::singleShot(
                    0, this,
                    [this, frame, callCommand]() {
                        completeCall(frame.requestId, callCommand);
                    });
            } else if (operation == lp3wire::MediaCommand) {
                QTimer::singleShot(
                    0, this,
                    [this, frame, mediaCommand]() {
                        completeMedia(frame.requestId, mediaCommand);
                    });
            } else if (operation == lp3wire::LocationQuery) {
                m_location.query(
                    frame.requestId,
                    locationQuery.accuracy == lp3wire::kLocationFine,
                    locationQuery.timeoutMs);
            } else if (operation == lp3wire::CalendarQuery) {
                m_calendarPending.insert(frame.requestId);
                m_calendar.query(frame.requestId, calendarQuery);
            } else if (operation == lp3wire::ContactQuery) {
                m_contactPending.insert(frame.requestId);
                m_contacts.query(frame.requestId, contactQuery);
            } else {
                QTimer::singleShot(
                    0, this,
                    [this, frame, pebbleBondRemove]() {
                        completePebbleBondRemove(
                            frame.requestId, pebbleBondRemove);
                    });
            }
            return true;
        }
        if (frame.type == lp3wire::Cancel) {
            if (frame.requestId == 0 || !frame.payload.empty()) {
                return false;
            }
            if (m_pending.remove(frame.requestId)) {
                m_location.cancel(frame.requestId);
                if (m_calendarPending.remove(frame.requestId)) {
                    m_calendar.cancel(frame.requestId);
                }
                if (m_contactPending.remove(frame.requestId)) {
                    m_contacts.cancel(frame.requestId);
                }
                return queueFrame(lp3wire::Cancelled, frame.requestId, NULL, 0);
            }
            return true;
        }
        return false;
    }

    void onReadable() {
        m_readNotifier.setEnabled(false);
        while (true) {
            lp3wire::Frame frame;
            const lp3wire::IoResult result =
                lp3wire::receiveFrameResult(m_fd, &frame, true);
            if (result == lp3wire::IoWouldBlock) {
                m_readNotifier.setEnabled(true);
                return;
            }
            if (result != lp3wire::IoFrame || !handleFrame(frame)) {
                failClosed();
                return;
            }
        }
    }

    void emitTimeChanged() {
        if (m_phase != Active) {
            return;
        }
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeTimeChanged(timeState(), &payload) ||
            !queueTimeChanged(payload)) {
            failClosed();
        }
    }

    void emitCalendarChanged() {
        if (m_phase != Active || !m_calendarReady) {
            return;
        }
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeCalendarChanged(&payload) ||
            !queueCalendarChanged(payload)) {
            failClosed();
        }
    }

    void emitContactChanged() {
        if (m_phase != Active || !m_contactsReady) return;
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeContactChanged(&payload) ||
            !queueContactChanged(payload)) {
            failClosed();
        }
    }

    void checkTimeChanged() {
        const qint64 wallMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 expectedWallMs = m_lastWallMs + m_elapsed.elapsed();
        const int offsetSeconds = QDateTime::currentDateTime().offsetFromUtc();
        const qint64 driftMs = wallMs > expectedWallMs ?
            wallMs - expectedWallMs : expectedWallMs - wallMs;
        if (driftMs > 2000 || offsetSeconds != m_lastOffsetSeconds) {
            emitTimeChanged();
        }
        m_lastWallMs = wallMs;
        m_lastOffsetSeconds = offsetSeconds;
        m_elapsed.restart();
    }

    enum Phase {
        AwaitHello,
        AwaitReady,
        Active,
    };

    int m_fd;
    Phase m_phase;
    QSocketNotifier m_readNotifier;
    QSocketNotifier m_writeNotifier;
    MDConfItem m_timeFormat;
    NotificationMonitor m_notifications;
    bool m_notificationsReady;
    bool m_messagingReady;
    CallMonitor m_calls;
    bool m_callsReady;
    MainVolumeMonitor m_media;
    bool m_mediaReady;
    MainVolumeMonitor::State m_mediaState;
    bool m_mediaStatePending;
    LocationMonitor m_location;
    bool m_locationReady;
    CalendarMonitor m_calendar;
    bool m_calendarReady;
    ContactMonitor m_contacts;
    bool m_contactsReady;
    QTimer m_changeTimer;
    QElapsedTimer m_elapsed;
    qint64 m_lastWallMs;
    int m_lastOffsetSeconds;
    QSet<quint64> m_pending;
    QSet<quint64> m_calendarPending;
    QSet<quint64> m_contactPending;
    std::deque<std::vector<uint8_t> > m_outgoing;
};

} // namespace

int main(int argc, char *argv[]) {
    if (!harden()) {
        return 126;
    }
    QCoreApplication application(argc, argv);
    HelperConnection connection(kSocketFd);
    return application.exec();
}
