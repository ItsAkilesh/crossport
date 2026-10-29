/*
 * File: shim/poll.c
 * Purpose: poll, ppoll, select and pselect6 over files, pipes, consoles and eventfds
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Readiness is computed per descriptor kind. Descriptors with a Win32 event (eventfds) are waited on with
 * WaitForMultipleObjects; pipes and consoles have no such event, so while any of them is being polled the
 * wait is cut into short slices and readiness is checked again after each one.
 */

#include "sock.h"

#define LINUX_POLLIN 0x001
#define LINUX_POLLOUT 0x004
#define LINUX_POLLERR 0x008
#define LINUX_POLLHUP 0x010
#define LINUX_POLLNVAL 0x020
#define POLL_SLICE_MS 2
#define SELECT_FD_LIMIT 1024
#define SELECT_WORD_BITS 64
#define NANOSECONDS_PER_MILLISECOND 1000000
#define MILLISECONDS_PER_SECOND 1000
#define MICROSECONDS_PER_MILLISECOND 1000

typedef struct {
    int fd;
    int16_t events;
    int16_t revents;
} PollFd;

typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} TimeSpec;

typedef struct {
    int64_t seconds;
    int64_t microseconds;
} TimeVal;

/* fd_set: 1024 bits in 64-bit words, least significant bit first. */
typedef struct {
    uint64_t words[SELECT_FD_LIMIT / SELECT_WORD_BITS];
} FdSet;

static short pipe_ready(int fd, short wanted)
{
    if (shim_fd_flags(fd) != LINUX_O_RDONLY) {
        return (short)(wanted & LINUX_POLLOUT);
    }
    DWORD available = 0;
    if (!PeekNamedPipe(shim_fd_handle(fd), NULL, 0, NULL, &available, NULL)) {
        return (short)(GetLastError() == ERROR_BROKEN_PIPE ? (LINUX_POLLIN | LINUX_POLLHUP) : LINUX_POLLERR);
    }
    return (short)(available > 0 ? (wanted & LINUX_POLLIN) : 0);
}

static short event_ready(int fd, short wanted)
{
    EventState *state = (EventState *)shim_fd_state(fd);
    short ready = 0;
    AcquireSRWLockShared(&state->lock);
    ready |= state->counter != 0 ? LINUX_POLLIN : 0;
    ready |= state->counter < UINT64_MAX - 1 ? LINUX_POLLOUT : 0;
    ReleaseSRWLockShared(&state->lock);
    return (short)(ready & wanted);
}

/* Console input is ready when its handle is signalled; every other plain file is always ready. */
static short file_ready(int fd, short wanted)
{
    HANDLE handle = shim_fd_handle(fd);
    DWORD mode;
    int is_console_input = shim_fd_flags(fd) == LINUX_O_RDONLY && GetConsoleMode(handle, &mode);
    if (is_console_input) {
        return (short)(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0 ? (wanted & LINUX_POLLIN) : 0);
    }
    return (short)(wanted & (LINUX_POLLIN | LINUX_POLLOUT));
}

/* Winsock's WSAPoll flag values differ from Linux's, so each direction is asked for and translated. */
static short socket_ready(int fd, short wanted)
{
    SocketState *state = (SocketState *)shim_fd_state(fd);
    SHORT asked = (SHORT)(((wanted & LINUX_POLLIN) ? POLLRDNORM : 0) | ((wanted & LINUX_POLLOUT) ? POLLWRNORM : 0));
    WSAPOLLFD entry = {.fd = state->socket, .events = asked};
    short ready = 0;
    if (WSAPoll(&entry, 1, 0) > 0) {
        ready |= (entry.revents & POLLRDNORM) ? LINUX_POLLIN : 0;
        ready |= (entry.revents & POLLWRNORM) ? LINUX_POLLOUT : 0;
        ready |= (entry.revents & POLLHUP) ? (LINUX_POLLHUP | LINUX_POLLIN) : 0;
        ready |= (entry.revents & POLLERR) ? LINUX_POLLERR : 0;
    }
    return ready;
}

short shim_fd_ready(int fd, short wanted)
{
    switch (shim_fd_kind(fd)) {
    case FD_KIND_SOCKET:
        return socket_ready(fd, wanted);
    case FD_KIND_PIPE:
        return pipe_ready(fd, wanted);
    case FD_KIND_EVENT:
        return event_ready(fd, wanted);
    default:
        return file_ready(fd, wanted);
    }
}

HANDLE shim_fd_wait_handle(int fd)
{
    return shim_fd_kind(fd) == FD_KIND_EVENT ? shim_fd_handle(fd) : NULL;
}

/* Fills revents for every entry and returns how many are ready (an invalid descriptor counts, as LINUX_POLLNVAL). */
static int scan(PollFd *fds, uint64_t count)
{
    int ready = 0;
    for (uint64_t i = 0; i < count; i++) {
        fds[i].revents = 0;
        if (fds[i].fd < 0) {
            continue;
        }
        if (shim_fd_flags(fds[i].fd) < 0) {
            fds[i].revents = LINUX_POLLNVAL;
        } else {
            fds[i].revents = shim_fd_ready(fds[i].fd, (short)(fds[i].events | LINUX_POLLHUP | LINUX_POLLERR));
        }
        ready += fds[i].revents != 0;
    }
    return ready;
}

static DWORD collect_handles(const PollFd *fds, uint64_t count, HANDLE *handles, int *is_polled)
{
    DWORD used = 0;
    *is_polled = 0;
    for (uint64_t i = 0; i < count; i++) {
        HANDLE handle = fds[i].fd >= 0 ? shim_fd_wait_handle(fds[i].fd) : NULL;
        if (handle != NULL && used < MAXIMUM_WAIT_OBJECTS) {
            handles[used++] = handle;
        } else if (fds[i].fd >= 0) {
            *is_polled = 1;
        }
    }
    return used;
}

static int64_t poll_loop(PollFd *fds, uint64_t count, int64_t timeout_ms)
{
    ULONGLONG start = GetTickCount64();
    for (;;) {
        int ready = scan(fds, count);
        int64_t elapsed = (int64_t)(GetTickCount64() - start);
        if (ready > 0 || timeout_ms == 0 || (timeout_ms > 0 && elapsed >= timeout_ms)) {
            return ready;
        }
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        int is_polled;
        DWORD used = collect_handles(fds, count, handles, &is_polled);
        DWORD slice = timeout_ms < 0 ? INFINITE : (DWORD)(timeout_ms - elapsed);
        if (is_polled && slice > POLL_SLICE_MS) {
            slice = POLL_SLICE_MS;
        }
        if (used > 0) {
            WaitForMultipleObjects(used, handles, FALSE, slice);
        } else if (slice != INFINITE) {
            Sleep(slice);
        } else {
            return shim_fail(LINUX_EINVAL); /* nothing can ever become ready */
        }
    }
}

LIBC_FN int sysv_poll(PollFd *fds, uint64_t count, int timeout_ms)
{
    return (int)poll_loop(fds, count, timeout_ms);
}

static int64_t milliseconds_of(const TimeSpec *timeout)
{
    if (timeout == NULL) {
        return -1;
    }
    int64_t whole = timeout->seconds * MILLISECONDS_PER_SECOND;
    return whole + (timeout->nanoseconds + NANOSECONDS_PER_MILLISECOND - 1) / NANOSECONDS_PER_MILLISECOND;
}

LIBC_FN int sysv_ppoll(PollFd *fds, uint64_t count, const TimeSpec *timeout, const void *mask)
{
    (void)mask;
    return (int)poll_loop(fds, count, milliseconds_of(timeout));
}

static int bit_is_set(const FdSet *set, int fd)
{
    return set != NULL && (set->words[fd / SELECT_WORD_BITS] >> (fd % SELECT_WORD_BITS) & 1u) != 0;
}

static void set_bit(FdSet *set, int fd)
{
    set->words[fd / SELECT_WORD_BITS] |= 1ull << (fd % SELECT_WORD_BITS);
}

/* select expressed as poll: one entry per descriptor named in any set, results written back to the sets. */
static int select_in_poll(int limit, FdSet *reads, FdSet *writes, FdSet *errors, int64_t timeout_ms)
{
    PollFd fds[SELECT_FD_LIMIT];
    int count = 0;
    if (limit < 0 || limit > SELECT_FD_LIMIT) {
        return shim_fail(LINUX_EINVAL);
    }
    for (int fd = 0; fd < limit; fd++) {
        int16_t events = (int16_t)((bit_is_set(reads, fd) ? LINUX_POLLIN : 0) | (bit_is_set(writes, fd) ? LINUX_POLLOUT : 0));
        if (events != 0 || bit_is_set(errors, fd)) {
            fds[count++] = (PollFd){.fd = fd, .events = events};
        }
    }
    int64_t ready = poll_loop(fds, (uint64_t)count, timeout_ms);
    if (ready < 0) {
        return -1;
    }
    FdSet out_reads = {{0}};
    FdSet out_writes = {{0}};
    int total = 0;
    for (int i = 0; i < count; i++) {
        int is_read = (fds[i].revents & (LINUX_POLLIN | LINUX_POLLHUP | LINUX_POLLERR)) != 0 && bit_is_set(reads, fds[i].fd);
        int is_write = (fds[i].revents & LINUX_POLLOUT) != 0 && bit_is_set(writes, fds[i].fd);
        if (is_read) {
            set_bit(&out_reads, fds[i].fd);
        }
        if (is_write) {
            set_bit(&out_writes, fds[i].fd);
        }
        total += is_read + is_write;
    }
    if (reads != NULL) {
        *reads = out_reads;
    }
    if (writes != NULL) {
        *writes = out_writes;
    }
    if (errors != NULL) {
        *errors = (FdSet){{0}};
    }
    return total;
}

LIBC_FN int sysv_select(int limit, FdSet *reads, FdSet *writes, FdSet *errors, const TimeVal *timeout)
{
    int64_t milliseconds = -1;
    if (timeout != NULL) {
        milliseconds = timeout->seconds * MILLISECONDS_PER_SECOND +
                       (timeout->microseconds + MICROSECONDS_PER_MILLISECOND - 1) / MICROSECONDS_PER_MILLISECOND;
    }
    return select_in_poll(limit, reads, writes, errors, milliseconds);
}

LIBC_FN int sysv_pselect6(int limit, FdSet *reads, FdSet *writes, FdSet *errors, const TimeSpec *timeout,
                          const void *mask)
{
    (void)mask;
    return select_in_poll(limit, reads, writes, errors, milliseconds_of(timeout));
}
