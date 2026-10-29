/*
 * File: shim/sock_opt.c
 * Purpose: setsockopt, getsockopt, sendmsg and recvmsg: the socket calls that need Linux constants translated
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "sock.h"

enum {
    LINUX_SO_REUSEADDR = 2, LINUX_SO_TYPE = 3, LINUX_SO_ERROR = 4, LINUX_SO_BROADCAST = 6, LINUX_SO_SNDBUF = 7,
    LINUX_SO_RCVBUF = 8, LINUX_SO_KEEPALIVE = 9, LINUX_SO_LINGER = 13, LINUX_SO_REUSEPORT = 15,
    LINUX_SO_RCVTIMEO = 20, LINUX_SO_SNDTIMEO = 21
};
enum { LINUX_TCP_NODELAY = 1 };
enum { MAX_IOVECS = 64, MILLISECONDS_PER_SECOND = 1000, MICROSECONDS_PER_MILLISECOND = 1000 };

typedef struct {
    int32_t onoff;
    int32_t linger;
} LinuxLinger;

typedef struct {
    int64_t seconds;
    int64_t microseconds;
} LinuxTimeVal;

typedef struct {
    void *base;
    size_t length;
} LinuxIoVec;

/* struct msghdr (56 bytes). */
typedef struct {
    void *name;
    uint32_t name_length;
    LinuxIoVec *vectors;
    size_t vector_count;
    void *control;
    size_t control_length;
    int32_t flags;
} LinuxMsgHdr;

static int level_to_windows(int level)
{
    return level == LINUX_SOL_SOCKET ? SOL_SOCKET : level;
}

/* The Winsock option number for a Linux SOL_SOCKET option, or -1 when it has no counterpart. */
static int socket_option_to_windows(int option)
{
    switch (option) {
    case LINUX_SO_REUSEADDR:
        return SO_REUSEADDR;
    case LINUX_SO_TYPE:
        return SO_TYPE;
    case LINUX_SO_ERROR:
        return SO_ERROR;
    case LINUX_SO_BROADCAST:
        return SO_BROADCAST;
    case LINUX_SO_SNDBUF:
        return SO_SNDBUF;
    case LINUX_SO_RCVBUF:
        return SO_RCVBUF;
    case LINUX_SO_KEEPALIVE:
        return SO_KEEPALIVE;
    case LINUX_SO_RCVTIMEO:
        return SO_RCVTIMEO;
    case LINUX_SO_SNDTIMEO:
        return SO_SNDTIMEO;
    default:
        return -1;
    }
}

static int set_timeout(SocketState *state, int option, const LinuxTimeVal *value)
{
    DWORD milliseconds = (DWORD)(value->seconds * MILLISECONDS_PER_SECOND + value->microseconds / MICROSECONDS_PER_MILLISECOND);
    return setsockopt(state->socket, SOL_SOCKET, option, (const char *)&milliseconds, sizeof(milliseconds));
}

static int set_linger(SocketState *state, const LinuxLinger *value)
{
    LINGER linger = {.l_onoff = (u_short)value->onoff, .l_linger = (u_short)value->linger};
    return setsockopt(state->socket, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
}

LIBC_FN int sysv_setsockopt(int fd, int level, int option, const void *value, uint32_t length)
{
    SocketState *state = shim_socket_state(fd);
    if (state == NULL) {
        return -1;
    }
    if (level == LINUX_SOL_SOCKET && option == LINUX_SO_REUSEPORT) {
        return 0; /* Windows has no SO_REUSEPORT; SO_REUSEADDR is the closest, and ignoring it is safe */
    }
    int status;
    if (level == LINUX_SOL_SOCKET && (option == LINUX_SO_RCVTIMEO || option == LINUX_SO_SNDTIMEO) && length >= sizeof(LinuxTimeVal)) {
        status = set_timeout(state, socket_option_to_windows(option), (const LinuxTimeVal *)value);
    } else if (level == LINUX_SOL_SOCKET && option == LINUX_SO_LINGER && length >= sizeof(LinuxLinger)) {
        status = set_linger(state, (const LinuxLinger *)value);
    } else {
        int windows_option = level == LINUX_SOL_SOCKET ? socket_option_to_windows(option) : option;
        if (windows_option < 0) {
            return shim_fail(LINUX_ENOPROTOOPT);
        }
        status = setsockopt(state->socket, level_to_windows(level), windows_option, (const char *)value, (int)length);
    }
    return status == 0 ? 0 : shim_fail_from_win();
}

LIBC_FN int sysv_getsockopt(int fd, int level, int option, void *value, uint32_t *length)
{
    SocketState *state = shim_socket_state(fd);
    if (state == NULL) {
        return -1;
    }
    if (level == LINUX_SOL_SOCKET && option == LINUX_SO_TYPE) {
        *(int32_t *)value = state->type;
        *length = sizeof(int32_t);
        return 0;
    }
    int windows_option = level == LINUX_SOL_SOCKET ? socket_option_to_windows(option) : option;
    if (windows_option < 0) {
        return shim_fail(LINUX_ENOPROTOOPT);
    }
    int size = (int)*length;
    if (getsockopt(state->socket, level_to_windows(level), windows_option, (char *)value, &size) != 0) {
        return shim_fail_from_win();
    }
    if (level == LINUX_SOL_SOCKET && option == LINUX_SO_ERROR) {
        *(int32_t *)value = *(int32_t *)value == 0 ? 0 : shim_errno_from_win((DWORD) * (int32_t *)value);
    }
    *length = (uint32_t)size;
    return 0;
}

/* Gathers an iovec array into one buffer; datagram and stream sends both work on the joined bytes. */
static int64_t join(const LinuxIoVec *vectors, size_t count, char **out)
{
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += vectors[i].length;
    }
    char *buffer = (char *)HeapAlloc(GetProcessHeap(), 0, total != 0 ? total : 1);
    if (buffer == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(buffer + at, vectors[i].base, vectors[i].length);
        at += vectors[i].length;
    }
    *out = buffer;
    return (int64_t)total;
}

LIBC_FN int64_t sysv_sendmsg(int fd, const LinuxMsgHdr *message, int flags)
{
    char *joined = NULL;
    if (message->vector_count > MAX_IOVECS) {
        return shim_fail(LINUX_EINVAL);
    }
    int64_t total = join(message->vectors, message->vector_count, &joined);
    if (total < 0) {
        return -1;
    }
    extern LIBC_FN int64_t sysv_sendto(int, const void *, size_t, int, const void *, uint32_t);
    int64_t sent = sysv_sendto(fd, joined, (size_t)total, flags, message->name, message->name_length);
    HeapFree(GetProcessHeap(), 0, joined);
    return sent;
}

/* Receives into a scratch buffer and scatters it across the vectors. */
LIBC_FN int64_t sysv_recvmsg(int fd, LinuxMsgHdr *message, int flags)
{
    size_t capacity = 0;
    if (message->vector_count > MAX_IOVECS) {
        return shim_fail(LINUX_EINVAL);
    }
    for (size_t i = 0; i < message->vector_count; i++) {
        capacity += message->vectors[i].length;
    }
    char *scratch = (char *)HeapAlloc(GetProcessHeap(), 0, capacity != 0 ? capacity : 1);
    if (scratch == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    extern LIBC_FN int64_t sysv_recvfrom(int, void *, size_t, int, void *, uint32_t *);
    int64_t got = sysv_recvfrom(fd, scratch, capacity, flags, message->name, &message->name_length);
    size_t copied = 0;
    for (size_t i = 0; got > 0 && i < message->vector_count && copied < (size_t)got; i++) {
        size_t part = message->vectors[i].length < (size_t)got - copied ? message->vectors[i].length : (size_t)got - copied;
        memcpy(message->vectors[i].base, scratch + copied, part);
        copied += part;
    }
    message->control_length = 0;
    message->flags = 0;
    HeapFree(GetProcessHeap(), 0, scratch);
    return got;
}
