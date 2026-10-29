/*
 * File: shim/fd_epoll.c
 * Purpose: epoll_create, epoll_ctl and epoll_wait on top of the shim's poll engine
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * An epoll descriptor holds a list of (descriptor, events, user data). epoll_wait turns the list into a
 * poll set and reports the ready ones. Delivery is level-triggered even when EPOLLET is requested: a
 * program that drains what it is told about sees the same behaviour, and one that does not simply is
 * told again. EPOLLONESHOT is honoured.
 */

#include <string.h>

#include "fd.h"

enum { EPOLL_CTL_ADD = 1, EPOLL_CTL_DEL = 2, EPOLL_CTL_MOD = 3 };
enum {
    EPOLLIN = 0x001, EPOLLPRI = 0x002, EPOLLOUT = 0x004, EPOLLERR = 0x008, EPOLLHUP = 0x010, EPOLLRDHUP = 0x2000,
    EPOLLONESHOT = 1 << 30
};
#define EPOLL_CLOEXEC LINUX_O_CLOEXEC
#define POLL_FLAG_MASK (EPOLLIN | EPOLLPRI | EPOLLOUT | EPOLLERR | EPOLLHUP)
#define MAX_WATCHED 1024

/* struct epoll_event is packed on x86-64: 12 bytes. */
typedef struct __attribute__((packed)) {
    uint32_t events;
    uint64_t data;
} EpollEvent;

_Static_assert(sizeof(EpollEvent) == 12, "struct epoll_event is 12 bytes on x86-64");

typedef struct {
    int fd;
    uint32_t events;
    uint64_t data;
    int is_disabled; /* EPOLLONESHOT fired; re-armed by EPOLL_CTL_MOD */
} Watch;

typedef struct {
    SRWLOCK lock;
    Watch watches[MAX_WATCHED];
    size_t count;
} EpollState;

LIBC_FN int sysv_epoll_create1(int flags)
{
    if ((flags & ~EPOLL_CLOEXEC) != 0) {
        return shim_fail(LINUX_EINVAL);
    }
    EpollState *state = (EpollState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EpollState));
    HANDLE placeholder = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (state == NULL || placeholder == NULL) {
        HeapFree(GetProcessHeap(), 0, state);
        return shim_fail(LINUX_ENOMEM);
    }
    InitializeSRWLock(&state->lock);
    int fd = shim_fd_adopt_kind(FD_KIND_EPOLL, placeholder, state, LINUX_O_RDWR);
    if (fd < 0) {
        CloseHandle(placeholder);
        HeapFree(GetProcessHeap(), 0, state);
    }
    return fd;
}

LIBC_FN int sysv_epoll_create(int size)
{
    return size > 0 ? sysv_epoll_create1(0) : shim_fail(LINUX_EINVAL);
}

int shim_epoll_close(void *state)
{
    HeapFree(GetProcessHeap(), 0, state);
    return 0;
}

static Watch *find_watch(EpollState *state, int fd)
{
    for (size_t i = 0; i < state->count; i++) {
        if (state->watches[i].fd == fd) {
            return &state->watches[i];
        }
    }
    return NULL;
}

static int epoll_state(int epfd, EpollState **out)
{
    if (shim_fd_flags(epfd) < 0) {
        return -1;
    }
    if (shim_fd_kind(epfd) != FD_KIND_EPOLL) {
        return shim_fail(LINUX_EINVAL);
    }
    *out = (EpollState *)shim_fd_state(epfd);
    return 0;
}

LIBC_FN int sysv_epoll_ctl(int epfd, int operation, int fd, const EpollEvent *event)
{
    EpollState *state;
    if (epoll_state(epfd, &state) != 0) {
        return -1;
    }
    if (shim_fd_flags(fd) < 0) {
        return -1;
    }
    int result = 0;
    AcquireSRWLockExclusive(&state->lock);
    Watch *existing = find_watch(state, fd);
    if (operation == EPOLL_CTL_ADD) {
        if (existing != NULL) {
            result = shim_fail(LINUX_EEXIST);
        } else if (state->count == MAX_WATCHED) {
            result = shim_fail(LINUX_ENOMEM);
        } else {
            state->watches[state->count++] = (Watch){.fd = fd, .events = event->events, .data = event->data};
        }
    } else if (existing == NULL) {
        result = shim_fail(LINUX_ENOENT);
    } else if (operation == EPOLL_CTL_MOD) {
        *existing = (Watch){.fd = fd, .events = event->events, .data = event->data};
    } else if (operation == EPOLL_CTL_DEL) {
        *existing = state->watches[--state->count];
    } else {
        result = shim_fail(LINUX_EINVAL);
    }
    ReleaseSRWLockExclusive(&state->lock);
    return result;
}

/* Copies the armed watches into a poll set; returns how many, and remembers which watch each entry is. */
static size_t snapshot(EpollState *state, ShimPollFd *fds, Watch *watches)
{
    size_t count = 0;
    AcquireSRWLockShared(&state->lock);
    for (size_t i = 0; i < state->count; i++) {
        if (!state->watches[i].is_disabled) {
            watches[count] = state->watches[i];
            fds[count] = (ShimPollFd){.fd = state->watches[i].fd, .events = (int16_t)(state->watches[i].events & POLL_FLAG_MASK)};
            count++;
        }
    }
    ReleaseSRWLockShared(&state->lock);
    return count;
}

static void disable_oneshot(EpollState *state, int fd)
{
    AcquireSRWLockExclusive(&state->lock);
    Watch *watch = find_watch(state, fd);
    if (watch != NULL) {
        watch->is_disabled = 1;
    }
    ReleaseSRWLockExclusive(&state->lock);
}

LIBC_FN int sysv_epoll_wait(int epfd, EpollEvent *events, int max_events, int timeout_ms)
{
    EpollState *state;
    ShimPollFd fds[MAX_WATCHED];
    Watch watches[MAX_WATCHED];
    if (epoll_state(epfd, &state) != 0) {
        return -1;
    }
    if (max_events <= 0) {
        return shim_fail(LINUX_EINVAL);
    }
    size_t count = snapshot(state, fds, watches);
    int64_t ready = shim_poll(fds, count, timeout_ms);
    if (ready < 0) {
        return -1;
    }
    int delivered = 0;
    for (size_t i = 0; i < count && delivered < max_events; i++) {
        if (fds[i].revents == 0) {
            continue;
        }
        events[delivered].events = (uint32_t)fds[i].revents;
        events[delivered].data = watches[i].data;
        delivered++;
        if (watches[i].events & EPOLLONESHOT) {
            disable_oneshot(state, watches[i].fd);
        }
    }
    return delivered;
}

LIBC_FN int sysv_epoll_pwait(int epfd, EpollEvent *events, int max_events, int timeout_ms, const void *mask)
{
    (void)mask;
    return sysv_epoll_wait(epfd, events, max_events, timeout_ms);
}
