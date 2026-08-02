/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal setgid entry point for the Sailfish user-session service.  It is
 * the only executable which gains the privileged group.  It starts the
 * package-owned daemon without that group and retains a private control
 * socket which can only start or stop the fixed Qt platform host.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
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
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "libpebble3d-launcher-wire.h"

#ifndef LP3_PLATFORM_BUILD_ID
#define LP3_PLATFORM_BUILD_ID "sailfish-provider-v1"
#endif

#ifndef LP3_LIBEXECDIR
#define LP3_LIBEXECDIR "/usr/libexec"
#endif

#define LP3_DAEMON_PATH LP3_LIBEXECDIR "/libpebble3d/libpebble3d"
#define LP3_HOST_PATH LP3_LIBEXECDIR "/libpebble3d/libpebble3d-platform-sailfish-host"
#define LP3_LAUNCHER_PATH LP3_LIBEXECDIR "/libpebble3d/libpebble3d-platform-sailfish-launcher"

static const char privileged_group_name[] = "privileged";
static const char platform_build_id[] = LP3_PLATFORM_BUILD_ID;
static volatile sig_atomic_t terminating;

static int startup_failure(const char *stage)
{
    dprintf(STDERR_FILENO, "libpebble3d launcher: %s failed\n", stage);
    return 126;
}

struct session_environment {
    uid_t uid;
    gid_t gid;
    gid_t privileged_gid;
    char home[PATH_MAX];
    char user[128];
    char runtime[PATH_MAX];
    char bus[PATH_MAX + 32];
    char adapter[64];
    char watch[32];
    unsigned int debug : 1;
    unsigned int verbose : 1;
    unsigned int ppog_verbose : 1;
    unsigned int autoconnect : 1;
    unsigned int trace_http : 1;
};

static void signal_handler(int signal_number)
{
    (void)signal_number;
    terminating = 1;
}

static void child_signal_handler(int signal_number)
{
    (void)signal_number;
}

static int package_owned(const char *path, uid_t owner, gid_t group,
                         mode_t required_mode)
{
    struct stat link_info;
    struct stat info;

    if (lstat(path, &link_info) != 0 || S_ISLNK(link_info.st_mode) ||
        stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        return 0;
    }
    return info.st_uid == owner && info.st_gid == group &&
           (info.st_mode & 07777) == required_mode;
}

static int validate_own_executable(gid_t privileged_gid)
{
    char path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);

    if (length <= 0 || length >= (ssize_t)(sizeof(path) - 1)) {
        return 0;
    }
    path[length] = '\0';
    return strcmp(path, LP3_LAUNCHER_PATH) == 0 &&
           package_owned(LP3_LAUNCHER_PATH, 0, privileged_gid, 02755);
}

static int read_file(const char *path, char *buffer, size_t size)
{
    int fd;
    ssize_t length;

    if (size == 0) {
        return 0;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return 0;
    }
    do {
        length = read(fd, buffer, size - 1);
    } while (length < 0 && errno == EINTR);
    close(fd);
    if (length < 0) {
        return 0;
    }
    buffer[length] = '\0';
    return 1;
}

/*
 * A direct caller would remain a Yama-approved ancestor of the daemon.  Only
 * the already-running user manager may parent this launcher, so asking the
 * real manager to start the fixed unit is harmless but direct exec fails.
 * The cgroup check prevents a nested same-UID systemd process from imitating
 * the session manager merely by executing the same root-owned pathname.
 */
static int cgroup_has_path(const char *cgroup, const char *expected)
{
    const size_t expected_length = strlen(expected);
    const char *line = cgroup;

    while (*line != '\0') {
        const char *line_end = strchr(line, '\n');
        const char *first_colon;
        const char *second_colon;
        size_t path_length;

        if (line_end == NULL) {
            line_end = line + strlen(line);
        }
        first_colon = memchr(line, ':', (size_t)(line_end - line));
        if (first_colon != NULL) {
            second_colon = memchr(first_colon + 1, ':',
                                  (size_t)(line_end - first_colon - 1));
            if (second_colon != NULL) {
                path_length = (size_t)(line_end - second_colon - 1);
                if (path_length == expected_length &&
                    memcmp(second_colon + 1, expected, expected_length) == 0) {
                    return 1;
                }
            }
        }
        line = *line_end == '\0' ? line_end : line_end + 1;
    }
    return 0;
}

static int validate_service_cgroup(uid_t uid)
{
    char cgroup[8192];
    char expected[192];

    if (!read_file("/proc/self/cgroup", cgroup, sizeof(cgroup))) {
        return 0;
    }
    if (snprintf(expected, sizeof(expected),
                 "/user.slice/user-%lu.slice/user@%lu.service/"
                 "libpebble3d.service",
                 (unsigned long)uid, (unsigned long)uid) >=
            (int)sizeof(expected)) {
        return 0;
    }
    return cgroup_has_path(cgroup, expected);
}

static int validate_user_manager_parent(uid_t uid, pid_t *manager_pid)
{
    const pid_t parent = getppid();
    char process_path[64];
    char executable[PATH_MAX];
    char cgroup[8192];
    char expected[160];
    struct stat info;
    ssize_t length;

    if (parent <= 1) {
        return 0;
    }
    snprintf(process_path, sizeof(process_path), "/proc/%ld/exe", (long)parent);
    length = readlink(process_path, executable, sizeof(executable) - 1);
    if (length <= 0 || length >= (ssize_t)(sizeof(executable) - 1)) {
        return 0;
    }
    executable[length] = '\0';
    if ((strcmp(executable, "/usr/lib/systemd/systemd") != 0 &&
         strcmp(executable, "/lib/systemd/systemd") != 0) ||
        stat(executable, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != 0 || (info.st_mode & 0022) != 0) {
        return 0;
    }
    snprintf(process_path, sizeof(process_path), "/proc/%ld", (long)parent);
    if (stat(process_path, &info) != 0 || info.st_uid != uid) {
        return 0;
    }
    snprintf(process_path, sizeof(process_path), "/proc/%ld/cgroup", (long)parent);
    if (!read_file(process_path, cgroup, sizeof(cgroup))) {
        return 0;
    }
    snprintf(expected, sizeof(expected),
             "/user.slice/user-%lu.slice/user@%lu.service/init.scope",
             (unsigned long)uid, (unsigned long)uid);
    if (!cgroup_has_path(cgroup, expected)) {
        return 0;
    }
    *manager_pid = parent;
    return 1;
}

static int valid_adapter(const char *value)
{
    const char prefix[] = "/org/bluez/hci";
    const char *cursor;

    if (value == NULL || strncmp(value, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }
    cursor = value + sizeof(prefix) - 1;
    if (*cursor == '\0') {
        return 0;
    }
    while (*cursor != '\0') {
        if (!isdigit((unsigned char)*cursor)) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int valid_watch_address(const char *value)
{
    size_t index;

    if (value == NULL || strlen(value) != 17) {
        return 0;
    }
    for (index = 0; index < 17; ++index) {
        if ((index + 1) % 3 == 0) {
            if (value[index] != ':') {
                return 0;
            }
        } else if (!isxdigit((unsigned char)value[index])) {
            return 0;
        }
    }
    return 1;
}

static int copy_session_environment(struct session_environment *session)
{
    struct passwd *password;
    struct group *privileged;
    const char *value;

    memset(session, 0, sizeof(*session));
    session->uid = getuid();
    session->gid = getgid();
    password = getpwuid(session->uid);
    privileged = getgrnam(privileged_group_name);
    if (password == NULL || privileged == NULL || password->pw_dir == NULL ||
        password->pw_name == NULL || password->pw_dir[0] != '/') {
        return 0;
    }
    session->privileged_gid = privileged->gr_gid;
    if (snprintf(session->home, sizeof(session->home), "%s", password->pw_dir) >=
            (int)sizeof(session->home) ||
        snprintf(session->user, sizeof(session->user), "%s", password->pw_name) >=
            (int)sizeof(session->user) ||
        snprintf(session->runtime, sizeof(session->runtime), "/run/user/%lu",
                 (unsigned long)session->uid) >= (int)sizeof(session->runtime) ||
        snprintf(session->bus, sizeof(session->bus),
                 "unix:path=/run/user/%lu/dbus/user_bus_socket",
                 (unsigned long)session->uid) >= (int)sizeof(session->bus)) {
        return 0;
    }
    value = getenv("LIBPEBBLE3_BLUEZ_ADAPTER");
    if (valid_adapter(value)) {
        snprintf(session->adapter, sizeof(session->adapter), "%s", value);
    }
    value = getenv("LIBPEBBLE3D_WATCH");
    if (valid_watch_address(value)) {
        snprintf(session->watch, sizeof(session->watch), "%s", value);
    }
    session->debug = getenv("LIBPEBBLE3D_DEBUG") != NULL &&
                     strcmp(getenv("LIBPEBBLE3D_DEBUG"), "1") == 0;
    session->verbose = getenv("LIBPEBBLE3D_VERBOSE") != NULL &&
                       strcmp(getenv("LIBPEBBLE3D_VERBOSE"), "1") == 0;
    session->ppog_verbose = getenv("LIBPEBBLE3D_PPOG_VERBOSE") != NULL &&
                            strcmp(getenv("LIBPEBBLE3D_PPOG_VERBOSE"), "1") == 0;
    session->autoconnect = getenv("LIBPEBBLE3D_AUTOCONNECT") != NULL &&
                           strcmp(getenv("LIBPEBBLE3D_AUTOCONNECT"), "1") == 0;
    session->trace_http = getenv("LIBPEBBLE3D_TRACE_HTTP") != NULL &&
                          strcmp(getenv("LIBPEBBLE3D_TRACE_HTTP"), "1") == 0;
    return 1;
}

static void reset_signals(void)
{
    struct sigaction action;
    sigset_t mask;
    int signal_number;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    for (signal_number = 1; signal_number < NSIG; ++signal_number) {
        if (signal_number != SIGKILL && signal_number != SIGSTOP) {
            sigaction(signal_number, &action, NULL);
        }
    }
    sigemptyset(&mask);
    sigprocmask(SIG_SETMASK, &mask, NULL);
}

static void close_from(int first)
{
    long maximum = sysconf(_SC_OPEN_MAX);
    int fd;

    if (maximum < first || maximum > 65536) {
        maximum = 65536;
    }
    for (fd = first; fd < maximum; ++fd) {
        close(fd);
    }
}

static int map_descriptor(int source, int destination)
{
    if (source != destination) {
        if (dup2(source, destination) < 0) {
            return 0;
        }
        close(source);
    }
    return fcntl(destination, F_SETFD, 0) == 0;
}

static void map_null_stdio(void)
{
    int input = open("/dev/null", O_RDONLY);
    int output = open("/dev/null", O_WRONLY);

    if (input < 0 || output < 0 || dup2(input, STDIN_FILENO) < 0 ||
        dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) {
        _exit(126);
    }
    if (input > STDERR_FILENO) {
        close(input);
    }
    if (output > STDERR_FILENO) {
        close(output);
    }
}

static void append_flag(char **environment, size_t *count, size_t maximum,
                        int enabled, char *value)
{
    if (enabled && *count + 1 < maximum) {
        environment[(*count)++] = value;
    }
}

static pid_t spawn_daemon(int socket_fd, const struct session_environment *session)
{
    const pid_t launcher_pid = getpid();
    pid_t child = fork();

    if (child != 0) {
        return child;
    }
    {
        char home[PATH_MAX + 8];
        char user[sizeof(session->user) + 8];
        char logname[sizeof(session->user) + 12];
        char runtime[PATH_MAX + 24];
        char bus[sizeof(session->bus) + 32];
        char adapter[sizeof(session->adapter) + 32];
        char watch[sizeof(session->watch) + 24];
        char *environment[24];
        size_t count = 0;
        char *arguments[] = { (char *)LP3_DAEMON_PATH, NULL };
        char debug[] = "LIBPEBBLE3D_DEBUG=1";
        char verbose[] = "LIBPEBBLE3D_VERBOSE=1";
        char ppog[] = "LIBPEBBLE3D_PPOG_VERBOSE=1";
        char autoconnect[] = "LIBPEBBLE3D_AUTOCONNECT=1";
        char trace_http[] = "LIBPEBBLE3D_TRACE_HTTP=1";

        reset_signals();
        if (prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) != 0 ||
            getppid() != launcher_pid ||
            setresgid(session->gid, session->gid, session->gid) != 0 ||
            setresuid(session->uid, session->uid, session->uid) != 0 ||
            !map_descriptor(socket_fd, LP3_LAUNCHER_CONTROL_FD)) {
            _exit(126);
        }
        map_null_stdio();
        close_from(LP3_LAUNCHER_CONTROL_FD + 1);
        umask(0077);
        if (chdir(session->home) != 0) {
            _exit(126);
        }
        snprintf(home, sizeof(home), "HOME=%s", session->home);
        snprintf(user, sizeof(user), "USER=%s", session->user);
        snprintf(logname, sizeof(logname), "LOGNAME=%s", session->user);
        snprintf(runtime, sizeof(runtime), "XDG_RUNTIME_DIR=%s", session->runtime);
        snprintf(bus, sizeof(bus), "DBUS_SESSION_BUS_ADDRESS=%s", session->bus);
        environment[count++] = (char *)"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
        environment[count++] = (char *)"LANG=C";
        environment[count++] = (char *)"LC_ALL=C";
        environment[count++] = home;
        environment[count++] = user;
        environment[count++] = logname;
        environment[count++] = runtime;
        environment[count++] = bus;
        environment[count++] = (char *)"LIBPEBBLE3_BLUEZ_PAIRING_BROKER=sailfish-lipstick";
        if (session->adapter[0] != '\0') {
            snprintf(adapter, sizeof(adapter), "LIBPEBBLE3_BLUEZ_ADAPTER=%s",
                     session->adapter);
            environment[count++] = adapter;
        }
        if (session->watch[0] != '\0') {
            snprintf(watch, sizeof(watch), "LIBPEBBLE3D_WATCH=%s", session->watch);
            environment[count++] = watch;
        }
        append_flag(environment, &count, 24, session->debug, debug);
        append_flag(environment, &count, 24, session->verbose, verbose);
        append_flag(environment, &count, 24, session->ppog_verbose, ppog);
        append_flag(environment, &count, 24, session->autoconnect, autoconnect);
        append_flag(environment, &count, 24, session->trace_http, trace_http);
        environment[count] = NULL;
        execve(LP3_DAEMON_PATH, arguments, environment);
        _exit(126);
    }
}

static pid_t spawn_host(int socket_fd, const struct session_environment *session)
{
    const pid_t launcher_pid = getpid();
    pid_t child = fork();

    if (child != 0) {
        return child;
    }
    {
        char parent_pid[64];
        char build_id[128];
        char home[PATH_MAX + 8];
        char user[sizeof(session->user) + 8];
        char runtime[PATH_MAX + 24];
        char bus[sizeof(session->bus) + 32];
        char *environment[12];
        char *arguments[] = { (char *)LP3_HOST_PATH, NULL };

        reset_signals();
        if (prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) != 0 ||
            getppid() != launcher_pid ||
            setresgid(session->privileged_gid, session->privileged_gid,
                      session->privileged_gid) != 0 ||
            !map_descriptor(socket_fd, LP3_LAUNCHER_CONTROL_FD)) {
            _exit(126);
        }
        map_null_stdio();
        close_from(LP3_LAUNCHER_CONTROL_FD + 1);
        umask(0077);
        if (chdir(session->home) != 0) {
            _exit(126);
        }
        snprintf(parent_pid, sizeof(parent_pid), "LP3_PARENT_PID=%ld",
                 (long)getppid());
        snprintf(build_id, sizeof(build_id), "LP3_PLATFORM_BUILD_ID=%s",
                 platform_build_id);
        snprintf(home, sizeof(home), "HOME=%s", session->home);
        snprintf(user, sizeof(user), "USER=%s", session->user);
        snprintf(runtime, sizeof(runtime), "XDG_RUNTIME_DIR=%s", session->runtime);
        snprintf(bus, sizeof(bus), "DBUS_SESSION_BUS_ADDRESS=%s", session->bus);
        environment[0] = (char *)"PATH=/usr/bin:/bin";
        environment[1] = (char *)"LANG=C";
        environment[2] = (char *)"LC_ALL=C";
        environment[3] = home;
        environment[4] = user;
        environment[5] = runtime;
        environment[6] = bus;
        environment[7] = parent_pid;
        environment[8] = build_id;
        environment[9] = NULL;
        execve(LP3_HOST_PATH, arguments, environment);
        _exit(126);
    }
}

static int send_message(int fd, uint16_t type, int32_t status, pid_t process_id,
                        int passed_fd)
{
    struct lp3_launcher_message_v1 message;
    struct iovec iov;
    struct msghdr header;
    unsigned char control[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *control_header;
    ssize_t written;

    memset(&message, 0, sizeof(message));
    message.magic = LP3_LAUNCHER_MAGIC;
    message.version = LP3_LAUNCHER_VERSION;
    message.type = type;
    message.status = status;
    message.process_id = (int32_t)process_id;
    memset(&header, 0, sizeof(header));
    iov.iov_base = &message;
    iov.iov_len = sizeof(message);
    header.msg_iov = &iov;
    header.msg_iovlen = 1;
    if (passed_fd >= 0) {
        memset(control, 0, sizeof(control));
        header.msg_control = control;
        header.msg_controllen = sizeof(control);
        control_header = CMSG_FIRSTHDR(&header);
        control_header->cmsg_level = SOL_SOCKET;
        control_header->cmsg_type = SCM_RIGHTS;
        control_header->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(control_header), &passed_fd, sizeof(passed_fd));
    }
    do {
        written = sendmsg(fd, &header, MSG_NOSIGNAL);
    } while (written < 0 && errno == EINTR);
    return written == (ssize_t)sizeof(message);
}

static int receive_message(int fd, struct lp3_launcher_message_v1 *message,
                           int timeout_ms)
{
    struct pollfd poll_fd;
    struct iovec iov;
    struct msghdr header;
    unsigned char control[CMSG_SPACE(sizeof(int) * 4)];
    struct cmsghdr *control_header;
    ssize_t received;
    int result;
    int unexpected_control;

    poll_fd.fd = fd;
    poll_fd.events = POLLIN | POLLHUP;
    poll_fd.revents = 0;
    do {
        result = poll(&poll_fd, 1, timeout_ms);
    } while (result < 0 && errno == EINTR && !terminating);
    if (result <= 0 || (poll_fd.revents & POLLIN) == 0) {
        return 0;
    }
    memset(&header, 0, sizeof(header));
    memset(control, 0, sizeof(control));
    iov.iov_base = message;
    iov.iov_len = sizeof(*message);
    header.msg_iov = &iov;
    header.msg_iovlen = 1;
    header.msg_control = control;
    header.msg_controllen = sizeof(control);
    do {
        received = recvmsg(fd, &header, MSG_CMSG_CLOEXEC | MSG_TRUNC);
    } while (received < 0 && errno == EINTR);
    unexpected_control = header.msg_controllen != 0;
    for (control_header = CMSG_FIRSTHDR(&header); control_header != NULL;
         control_header = CMSG_NXTHDR(&header, control_header)) {
        if (control_header->cmsg_level == SOL_SOCKET &&
            control_header->cmsg_type == SCM_RIGHTS &&
            control_header->cmsg_len >= CMSG_LEN(0)) {
            const size_t count =
                (control_header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int *descriptors = (const int *)CMSG_DATA(control_header);
            size_t index;

            for (index = 0; index < count; ++index) {
                close(descriptors[index]);
            }
        }
    }
    if (received != (ssize_t)sizeof(*message) ||
        (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0 ||
        unexpected_control || message->magic != LP3_LAUNCHER_MAGIC ||
        message->version != LP3_LAUNCHER_VERSION || message->status != 0 ||
        message->process_id != 0) {
        return 0;
    }
    return 1;
}

static void stop_process(pid_t *process)
{
    unsigned int attempt;
    int status;

    if (*process <= 0) {
        return;
    }
    kill(*process, SIGTERM);
    for (attempt = 0; attempt < 40; ++attempt) {
        pid_t result = waitpid(*process, &status, WNOHANG);
        if (result == *process || (result < 0 && errno == ECHILD)) {
            *process = -1;
            return;
        }
        {
            const struct timespec delay = { 0, 50 * 1000 * 1000 };
            nanosleep(&delay, NULL);
        }
    }
    kill(*process, SIGKILL);
    while (waitpid(*process, &status, 0) < 0 && errno == EINTR) {
    }
    *process = -1;
}

static void reap_process(pid_t *process)
{
    int status;
    pid_t result;

    if (*process <= 0) {
        return;
    }
    do {
        result = waitpid(*process, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == *process || (result < 0 && errno == ECHILD)) {
        *process = -1;
    }
}

static int install_signal_handlers(void)
{
    struct sigaction action;
    struct sigaction child_action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    memset(&child_action, 0, sizeof(child_action));
    child_action.sa_handler = child_signal_handler;
    sigemptyset(&child_action.sa_mask);
    return sigaction(SIGTERM, &action, NULL) == 0 &&
           sigaction(SIGINT, &action, NULL) == 0 &&
           sigaction(SIGHUP, &action, NULL) == 0 &&
           sigaction(SIGCHLD, &child_action, NULL) == 0;
}

int main(int argc, char **argv)
{
    struct session_environment session;
    struct rlimit core_limit;
    struct lp3_launcher_message_v1 message;
    gid_t real_group;
    gid_t effective_group;
    gid_t saved_group;
    int control[2] = { -1, -1 };
    pid_t daemon_pid = -1;
    pid_t host_pid = -1;
    pid_t manager_pid = -1;
    int manager_valid;
    int service_cgroup_valid;
    int exit_status = 1;

    (void)argv;
    if (argc != 1) {
        return startup_failure("argument validation");
    }
    if (!copy_session_environment(&session)) {
        return startup_failure("session identity validation");
    }
    if (getresgid(&real_group, &effective_group, &saved_group) != 0 ||
        real_group != session.gid ||
        effective_group != session.privileged_gid ||
        saved_group != session.privileged_gid) {
        return startup_failure("setgid transition");
    }
    if (!validate_own_executable(session.privileged_gid)) {
        return startup_failure("launcher package validation");
    }
    if (!package_owned(LP3_DAEMON_PATH, 0, 0, 0755) ||
        !package_owned(LP3_HOST_PATH, 0, session.privileged_gid, 0750)) {
        return startup_failure("child package validation");
    }
    /*
     * Procfs gates /proc/PID/exe through ptrace-style filesystem credential
     * checks.  The setgid effective group would make the real session manager
     * look cross-credential even though it has our real UID/GID.  Use the
     * session GID only for that read-only validation, retaining the packaged
     * privileged GID solely as the saved ID, then restore it before launch.
     */
    if (setegid(session.gid) != 0) {
        return startup_failure("session credential validation");
    }
    manager_valid = validate_user_manager_parent(session.uid, &manager_pid);
    service_cgroup_valid = validate_service_cgroup(session.uid);
    if (setegid(session.privileged_gid) != 0) {
        return startup_failure("privileged credential restoration");
    }
    if (!manager_valid) {
        return startup_failure("user-manager validation");
    }
    if (!service_cgroup_valid) {
        return startup_failure("service cgroup validation");
    }
    if (prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) != 0 ||
        getppid() != manager_pid) {
        return startup_failure("user-manager lifetime validation");
    }
    if (clearenv() != 0 || chdir("/") != 0) {
        return startup_failure("environment sanitization");
    }
    umask(0077);
    close_from(STDERR_FILENO + 1);
    core_limit.rlim_cur = 0;
    core_limit.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0 ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control) != 0) {
        return startup_failure("launcher hardening");
    }
    daemon_pid = spawn_daemon(control[1], &session);
    if (daemon_pid <= 0) {
        close(control[0]);
        close(control[1]);
        return startup_failure("daemon process creation");
    }
    close(control[1]);
    control[1] = -1;
    if (!install_signal_handlers() ||
        !receive_message(control[0], &message, 30000) ||
        message.type != LP3_LAUNCHER_DAEMON_READY) {
        stop_process(&daemon_pid);
        close(control[0]);
        return startup_failure("daemon secure handshake");
    }

    while (!terminating) {
        struct pollfd poll_fd;
        int poll_result;
        int daemon_status;
        pid_t daemon_result;

        reap_process(&host_pid);
        do {
            daemon_result = waitpid(daemon_pid, &daemon_status, WNOHANG);
        } while (daemon_result < 0 && errno == EINTR);
        if (daemon_result == daemon_pid || (daemon_result < 0 && errno == ECHILD)) {
            if (daemon_result == daemon_pid && WIFEXITED(daemon_status)) {
                exit_status = WEXITSTATUS(daemon_status);
            }
            daemon_pid = -1;
            break;
        }
        poll_fd.fd = control[0];
        poll_fd.events = POLLIN | POLLHUP;
        poll_fd.revents = 0;
        do {
            poll_result = poll(&poll_fd, 1, 1000);
        } while (poll_result < 0 && errno == EINTR && !terminating);
        if (terminating) {
            break;
        }
        if (poll_result < 0 || (poll_result > 0 && (poll_fd.revents & POLLHUP) != 0 &&
                                (poll_fd.revents & POLLIN) == 0)) {
            break;
        }
        if (poll_result == 0 || (poll_fd.revents & POLLIN) == 0) {
            continue;
        }
        if (!receive_message(control[0], &message, 0)) {
            break;
        }
        if (message.type == LP3_LAUNCHER_START_HOST) {
            int sockets[2] = { -1, -1 };

            stop_process(&host_pid);
            if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
                if (!send_message(control[0], LP3_LAUNCHER_ERROR, errno, 0, -1)) {
                    break;
                }
                continue;
            }
            host_pid = spawn_host(sockets[1], &session);
            close(sockets[1]);
            if (host_pid <= 0 ||
                !send_message(control[0], LP3_LAUNCHER_HOST_STARTED, 0,
                              host_pid, sockets[0])) {
                close(sockets[0]);
                stop_process(&host_pid);
                if (host_pid <= 0) {
                    (void)send_message(control[0], LP3_LAUNCHER_ERROR,
                                       EIO, 0, -1);
                }
                continue;
            }
            close(sockets[0]);
        } else if (message.type == LP3_LAUNCHER_STOP_HOST) {
            stop_process(&host_pid);
            if (!send_message(control[0], LP3_LAUNCHER_HOST_STOPPED, 0, 0, -1)) {
                break;
            }
        } else {
            break;
        }
    }

    stop_process(&host_pid);
    stop_process(&daemon_pid);
    close(control[0]);
    return terminating ? 0 : exit_status;
}
