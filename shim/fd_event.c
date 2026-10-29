/*
 * File: shim/fd_event.c
 * Purpose: eventfd descriptors: a 64-bit counter that can be waited on with read and poll
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Event loops (GLib, Qt, libuv) use an eventfd to wake a thread that is blocked in poll. The counter
 * lives in the descriptor, and a manual-reset Win32 event is signalled while it is not zero, so the
 * same event serves both blocking reads and poll.
 */

#include "fd.h"

#define EFD_SEMAPHORE 1
#define EFD_NONBLOCK LINUX_O_NONBLOCK
#define EFD_VALID_FLAGS (EFD_SEMAPHORE | EFD_NONBLOCK | LINUX_O_CLOEXEC)
#define EVENTFD_MAX (UINT64_MAX - 1)

LIBC_FN int sysv_eventfd(unsigned initial, int flags)
{
    if ((flags & ~EFD_VALID_FLAGS) != 0) {
        return shim_fail(LINUX_EINVAL);
    }
    EventState *state = (EventState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EventState));
    HANDLE event = CreateEventW(NULL, TRUE, initial != 0, NULL);
    if (state == NULL || event == NULL) {
        HeapFree(GetProcessHeap(), 0, state);
        return event == NULL ? shim_fail_from_win() : shim_fail(LINUX_ENOMEM);
    }
    InitializeSRWLock(&state->lock);
    state->counter = initial;
    state->is_semaphore = (flags & EFD_SEMAPHORE) != 0;
    state->event = event;
    int fd = shim_fd_adopt_kind(FD_KIND_EVENT, event, state, LINUX_O_RDWR | (flags & EFD_NONBLOCK));
    if (fd < 0) {
        CloseHandle(event);
        HeapFree(GetProcessHeap(), 0, state);
    }
    return fd;
}

/* Takes the counter (or one unit of it) once it is not zero; waits for that unless the descriptor is non-blocking. */
int64_t shim_event_read(int fd, void *buffer, size_t count)
{
    EventState *state = (EventState *)shim_fd_state(fd);
    if (state == NULL || count < sizeof(uint64_t)) {
        return shim_fail(LINUX_EINVAL);
    }
    for (;;) {
        AcquireSRWLockExclusive(&state->lock);
        if (state->counter != 0) {
            uint64_t taken = state->is_semaphore ? 1 : state->counter;
            state->counter -= taken;
            if (state->counter == 0) {
                ResetEvent(state->event);
            }
            ReleaseSRWLockExclusive(&state->lock);
            *(uint64_t *)buffer = taken;
            return sizeof(uint64_t);
        }
        ReleaseSRWLockExclusive(&state->lock);
        if ((shim_fd_flags(fd) & LINUX_O_NONBLOCK) != 0) {
            return shim_fail(LINUX_EAGAIN);
        }
        WaitForSingleObject(state->event, INFINITE);
    }
}

int64_t shim_event_write(int fd, const void *buffer, size_t count)
{
    EventState *state = (EventState *)shim_fd_state(fd);
    if (state == NULL || count < sizeof(uint64_t)) {
        return shim_fail(LINUX_EINVAL);
    }
    uint64_t added = *(const uint64_t *)buffer;
    if (added == UINT64_MAX) {
        return shim_fail(LINUX_EINVAL);
    }
    AcquireSRWLockExclusive(&state->lock);
    int is_full = state->counter > EVENTFD_MAX - added;
    if (!is_full) {
        state->counter += added;
        if (state->counter != 0) {
            SetEvent(state->event);
        }
    }
    ReleaseSRWLockExclusive(&state->lock);
    return is_full ? shim_fail(LINUX_EAGAIN) : (int64_t)sizeof(uint64_t);
}

LIBC_FN int sysv_eventfd_read(int fd, uint64_t *value)
{
    return shim_event_read(fd, value, sizeof(*value)) < 0 ? -1 : 0;
}

LIBC_FN int sysv_eventfd_write(int fd, uint64_t value)
{
    return shim_event_write(fd, &value, sizeof(value)) < 0 ? -1 : 0;
}
