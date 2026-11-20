/* Scoped file access for the unprivileged daemon. */
#define _GNU_SOURCE
#include <jni.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

enum operation { PROBE, STAT, LIST, READ, WRITE, MKDIR, REMOVE };
enum status { OK, MISSING, DENIED, WRONG_TYPE, INVALID, IO_ERROR };
struct buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
};

/* JNI returns the whole result as one Java byte array. */
#define MAX_REPLY (64U * 1024 * 1024)
#define DIRECTORY_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)

static enum status failure(void)
{
    switch (errno) {
    case ENOENT: return MISSING;
    case EACCES: case EPERM: case ESTALE: case ELOOP: return DENIED;
    case ENOTDIR: case EISDIR: return WRONG_TYPE;
    case EINVAL: case ENAMETOOLONG: case ENOTEMPTY: return INVALID;
    default: return IO_ERROR;
    }
}

static int valid_name(const char *name, size_t len)
{
    return len && len <= NAME_MAX && !memchr(name, 0, len)
        && !memchr(name, '/', len) && !memchr(name, '\\', len)
        && !(len == 1 && name[0] == '.')
        && !(len == 2 && name[0] == '.' && name[1] == '.');
}

static unsigned char *append(struct buffer *buf, size_t len)
{
    if (len > MAX_REPLY - buf->size) return NULL;
    size_t needed = buf->size + len;
    if (needed > buf->capacity) {
        size_t capacity = buf->capacity ? 2 * buf->capacity : 128;
        if (capacity < needed) capacity = needed;
        if (capacity > MAX_REPLY) capacity = MAX_REPLY;
        void *grown = realloc(buf->data, capacity);
        if (!grown) return NULL;
        buf->data = grown;
        buf->capacity = capacity;
    }
    unsigned char *out = buf->data + buf->size;
    buf->size = needed;
    return out;
}

static void encode(unsigned char *out, uint64_t value, size_t bytes)
{
    while (bytes) {
        out[--bytes] = (unsigned char)value;
        value >>= 8;
    }
}

static enum status entry(struct buffer *buf, const char *name, const struct stat *st)
{
    if (!S_ISDIR(st->st_mode) && !S_ISREG(st->st_mode)) return WRONG_TYPE;
    size_t len = strlen(name);
    unsigned char *p = append(buf, 21 + len);
    if (!p) return IO_ERROR;
    p[0] = S_ISDIR(st->st_mode) ? 1 : 0;
    encode(p + 1, st->st_size, 8);
    encode(p + 9, (uint64_t)st->st_mtim.tv_sec * 1000 + st->st_mtim.tv_nsec / 1000000, 8);
    encode(p + 17, len, 4);
    memcpy(p + 21, name, len);
    return OK;
}

static enum status descend(int *fd, const char *name)
{
    int next = openat(*fd, name, DIRECTORY_FLAGS);
    if (next < 0) return failure();
    close(*fd);
    *fd = next;
    return OK;
}

static enum status open_root(JNIEnv *env, jbyteArray root, int *fd)
{
    jsize len = (*env)->GetArrayLength(env, root);
    if (len < 1 || len >= PATH_MAX) return INVALID;
    char path[PATH_MAX];
    (*env)->GetByteArrayRegion(env, root, 0, len, (jbyte *)path);
    if ((*env)->ExceptionCheck(env)) return IO_ERROR;
    if (path[0] != '/' || memchr(path, 0, len)) return INVALID;
    path[len] = 0;
    *fd = open("/", DIRECTORY_FLAGS);
    if (*fd < 0) return failure();
    char *next = path + 1;
    while (*next) {
        char *name = strsep(&next, "/");
        if (!valid_name(name, strlen(name))) return INVALID;
        enum status status = descend(fd, name);
        if (status != OK || !next) return status;
    }
    return OK;
}

static enum status part(JNIEnv *env, jobjectArray parts, jsize i, char *name)
{
    jbyteArray value = (*env)->GetObjectArrayElement(env, parts, i);
    if (!value) return INVALID;
    jsize len = (*env)->GetArrayLength(env, value);
    enum status status = INVALID;
    if (len > 0 && len <= NAME_MAX) {
        (*env)->GetByteArrayRegion(env, value, 0, len, (jbyte *)name);
        if (!(*env)->ExceptionCheck(env) && valid_name(name, len)) {
            name[len] = 0;
            status = OK;
        }
    }
    (*env)->DeleteLocalRef(env, value);
    return status;
}

static enum status list_entries(int fd, struct buffer *result)
{
    int copy = dup(fd);
    if (copy < 0) return failure();
    DIR *dir = fdopendir(copy);
    if (!dir) {
        enum status status = failure();
        close(copy);
        return status;
    }
    enum status status = OK;
    for (;;) {
        errno = 0;
        struct dirent *item = readdir(dir);
        if (!item) {
            if (errno) status = failure();
            break;
        }
        if (!valid_name(item->d_name, strlen(item->d_name))) continue;
        struct stat st;
        if (fstatat(fd, item->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
            if (errno == ENOENT) continue;
            status = failure();
        } else {
            status = entry(result, item->d_name, &st);
            if (status == WRONG_TYPE) { status = OK; continue; }
        }
        if (status != OK) break;
    }
    closedir(dir);
    return status;
}

/* Pin and inspect the inode before opening it for I/O (including device nodes). */
static enum status open_regular(int parent, const char *name, int flags, int *fd, struct stat *st)
{
    int pinned = openat(parent, name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (pinned < 0) return failure();
    enum status status = OK;
    if (fstat(pinned, st) < 0) status = failure();
    else if (!S_ISREG(st->st_mode)) status = WRONG_TYPE;
    else {
        char path[48];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", pinned);
        *fd = open(path, flags | O_CLOEXEC | O_NONBLOCK);
        if (*fd < 0) status = failure();
    }
    close(pinned);
    return status;
}

static enum status read_file(int parent, const char *name, struct buffer *buf)
{
    struct stat st;
    int fd = -1;
    enum status status = open_regular(parent, name, O_RDONLY, &fd, &st);
    if (status != OK) return status;
    if (st.st_size > MAX_REPLY) {
        close(fd);
        return IO_ERROR;
    }
    unsigned char block[16384];
    for (;;) {
        ssize_t n = read(fd, block, sizeof(block));
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) break;
        if (n < 0) { status = failure(); break; }
        unsigned char *out = append(buf, (size_t)n);
        if (!out) { status = IO_ERROR; break; }
        memcpy(out, block, (size_t)n);
    }
    close(fd);
    return status;
}

static enum status preserve_access(int source, const struct stat *original, int replacement)
{
    struct stat st;
    if (fstat(replacement, &st) < 0) return failure();
    if ((st.st_uid != original->st_uid || st.st_gid != original->st_gid) &&
        fchown(replacement, original->st_uid, original->st_gid) < 0) return failure();
    const char acl[] = "system.posix_acl_access";
    ssize_t len = fgetxattr(source, acl, NULL, 0);
    if (len < 0) {
        if (errno != ENODATA && errno != ENOTSUP) return failure();
        if (fremovexattr(replacement, acl) < 0 && errno != ENODATA && errno != ENOTSUP)
            return failure();
    } else if (len > 0) {
        void *value = malloc((size_t)len);
        if (!value) return IO_ERROR;
        enum status status = OK;
        ssize_t got = fgetxattr(source, acl, value, (size_t)len);
        if (got < 0 || fsetxattr(replacement, acl, value, (size_t)got, 0) < 0)
            status = failure();
        free(value);
        if (status != OK) return status;
    }
    return fchmod(replacement, original->st_mode & 0777) == 0 ? OK : failure();
}

static enum status replace_contents(JNIEnv *env, int parent, const char *name, jbyteArray payload,
                                    int existing, const struct stat *original)
{
    char temp[64];
    int random = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random < 0) return failure();
    uint64_t nonce;
    ssize_t n;
    do { n = read(random, &nonce, sizeof(nonce)); } while (n < 0 && errno == EINTR);
    close(random);
    if (n != sizeof(nonce)) return IO_ERROR;
    snprintf(temp, sizeof(temp), ".rockpool-%016" PRIx64, nonce);
    int fd = openat(parent, temp, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return failure();
    enum status status = OK;
    jsize size = (*env)->GetArrayLength(env, payload);
    jbyte block[16384];
    for (jsize offset = 0; offset < size && status == OK;) {
        jsize len = size - offset;
        if ((size_t)len > sizeof(block)) len = sizeof(block);
        (*env)->GetByteArrayRegion(env, payload, offset, len, block);
        if ((*env)->ExceptionCheck(env)) { status = IO_ERROR; break; }
        for (jsize pos = 0; pos < len;) {
            n = write(fd, block + pos, len - pos);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { status = IO_ERROR; break; }
            pos += n;
        }
        offset += len;
    }
    if (status == OK && existing >= 0) status = preserve_access(existing, original, fd);
    if (status == OK && fsync(fd) < 0) status = failure();
    if (close(fd) < 0 && status == OK) status = failure();
    if (status == OK && renameat(parent, temp, parent, name) < 0) status = failure();
    if (status != OK) unlinkat(parent, temp, 0);
    return status;
}

static enum status write_file(JNIEnv *env, int parent, const char *name, jbyteArray payload, int create)
{
    if (!payload) return INVALID;
    if ((size_t)(*env)->GetArrayLength(env, payload) > MAX_REPLY) return IO_ERROR;
    struct stat original;
    int existing = -1;
    enum status lookup = open_regular(parent, name, O_WRONLY, &existing, &original);
    if (lookup != OK && (lookup != MISSING || !create)) return lookup;
    enum status status = replace_contents(env, parent, name, payload, existing, &original);
    if (existing >= 0) close(existing);
    return status;
}

static enum status remove_entry(int parent, const char *name, int recursive, unsigned depth)
{
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) < 0) return failure();
    if (!S_ISDIR(st.st_mode)) return unlinkat(parent, name, 0) == 0 ? OK : failure();
    if (recursive) {
        if (depth >= 128) return INVALID;
        int fd = openat(parent, name, DIRECTORY_FLAGS);
        if (fd < 0) return failure();
        struct stat from, into;
        enum status status = OK;
        if (fstat(parent, &from) < 0 || fstat(fd, &into) < 0) status = failure();
        else if (from.st_dev != into.st_dev) status = DENIED;
        DIR *dir = status == OK ? fdopendir(fd) : NULL;
        if (!dir) {
            if (status == OK) status = failure();
            close(fd);
            return status;
        }
        while (status == OK) {
            errno = 0;
            struct dirent *child = readdir(dir);
            if (!child) {
                if (errno) status = failure();
                break;
            }
            if (!strcmp(child->d_name, ".") || !strcmp(child->d_name, "..")) continue;
            status = remove_entry(fd, child->d_name, 1, depth + 1);
            if (status == MISSING) status = OK;
        }
        closedir(dir);
        if (status != OK) return status;
    }
    return unlinkat(parent, name, AT_REMOVEDIR) == 0 ? OK : failure();
}

static enum status operate(JNIEnv *env, int *fd, jint op, jobjectArray parts, jbyteArray payload,
                           jboolean flag, struct buffer *result)
{
    jsize count = (*env)->GetArrayLength(env, parts);
    if (!count && op != STAT && op != LIST) return INVALID;
    char name[NAME_MAX + 1] = "";
    for (jsize i = 0; i < count; ++i) {
        enum status status = part(env, parts, i, name);
        if (status != OK) return status;
        if (i + 1 < count || op == LIST) {
            status = descend(fd, name);
            if (status != OK) return status;
        }
    }
    struct stat st;
    switch (op) {
    case STAT:
        if ((count ? fstatat(*fd, name, &st, AT_SYMLINK_NOFOLLOW) : fstat(*fd, &st)) < 0)
            return failure();
        return entry(result, name, &st);
    case LIST: return list_entries(*fd, result);
    case READ: return read_file(*fd, name, result);
    case WRITE: return write_file(env, *fd, name, payload, flag);
    case MKDIR:
        if (mkdirat(*fd, name, 0700) == 0) return OK;
        if (errno != EEXIST || fstatat(*fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) return failure();
        return S_ISDIR(st.st_mode) ? OK : WRONG_TYPE;
    case REMOVE: return remove_entry(*fd, name, flag, 0);
    default: return INVALID;
    }
}

JNIEXPORT jbyteArray JNICALL Java_io_rebble_libpebblecommon_rockpool_PlatformFilesNative_request(
        JNIEnv *env, jclass cls, jint op, jbyteArray root, jlong device, jlong inode,
        jobjectArray parts, jbyteArray payload, jboolean flag)
{
    (void)cls;
    int fd = -1;
    struct buffer buf = {0};
    enum status status = root && parts ? open_root(env, root, &fd) : INVALID;
    if (status == OK) {
        struct stat st;
        if (fstat(fd, &st) < 0) status = failure();
        else if (op == PROBE) {
            unsigned char *p = append(&buf, 16);
            if (!p) status = IO_ERROR;
            else {
                encode(p, st.st_dev, 8);
                encode(p + 8, st.st_ino, 8);
            }
        } else if ((uint64_t)device != st.st_dev || (uint64_t)inode != st.st_ino) status = DENIED;
        else status = operate(env, &fd, op, parts, payload, flag, &buf);
    }
    if (fd >= 0) close(fd);
    jbyteArray reply = NULL;
    if (!(*env)->ExceptionCheck(env)) {
        jsize len = status == OK ? (jsize)buf.size : 0;
        reply = (*env)->NewByteArray(env, len + 1);
        if (reply) {
            jbyte code = status;
            (*env)->SetByteArrayRegion(env, reply, 0, 1, &code);
            if (len) (*env)->SetByteArrayRegion(env, reply, 1, len, (const jbyte *)buf.data);
        }
    }
    free(buf.data);
    return reply;
}
