/*
 * File: shim/fd_timer.c
 * Purpose: timerfd_create, timerfd_settime, timerfd_gettime and reading expiration counts
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A timerfd is a manual-reset Win32 waitable timer. The expiry time and interval are kept in the
 * descriptor so a read can say how many expirations passed, and re-arm the timer for the next one.
 */

#include "fd.h"

#define TFD_TIMER_ABSTIME 1
#define TFD_NONBLOCK LINUX_O_NONBLOCK
#define TFD_VALID_FLAGS (TFD_NONBLOCK | LINUX_O_CLOEXEC)
#define NANOSECONDS_PER_100NS 100
#define UNITS_PER_SECOND 10000000ll
#define NEVER_DUE_100NS (-0x7fffffffffffffffll)
#define CLOCK_REALTIME_ID 0
#define CLOCK_MONOTONIC_ID 1
#define WAIT_SLICE_MS 50
#define UNIX_EPOCH_100NS 116444736000000000ll

/* struct itimerspec. */
typedef struct {
    int64_t interval_seconds;
    int64_t interval_nanoseconds;
    int64_t value_seconds;
    int64_t value_nanoseconds;
} ITimerSpec;

static int64_t to_100ns(int64_t seconds, int64_t nanoseconds)
{
    return seconds * UNITS_PER_SECOND + nanoseconds / NANOSECONDS_PER_100NS;
}

/* The time on the timer's clock in 100 ns units: wall-clock time since the Unix epoch, or the monotonic counter. */
static int64_t now_100ns(int clock)
{
    if (clock == CLOCK_REALTIME_ID) {
        FILETIME now;
        GetSystemTimePreciseAsFileTime(&now);
        return (int64_t)(((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime) - UNIX_EPOCH_100NS;
    }
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (int64_t)((uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart * UNITS_PER_SECOND +
                     (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart * UNITS_PER_SECOND / (uint64_t)frequency.QuadPart);
}

LIBC_FN int sysv_timerfd_create(int clock, int flags)
{
    if ((flags & ~TFD_VALID_FLAGS) != 0 || (clock != CLOCK_REALTIME_ID && clock != CLOCK_MONOTONIC_ID)) {
        return shim_fail(LINUX_EINVAL);
    }
    HANDLE timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    TimerState *state = (TimerState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TimerState));
    if (timer == NULL || state == NULL) {
        HeapFree(GetProcessHeap(), 0, state);
        if (timer != NULL) {
            CloseHandle(timer);
        }
        return shim_fail(LINUX_ENOMEM);
    }
    *state = (TimerState){.timer = timer, .clock = clock};
    int fd = shim_fd_adopt_kind(FD_KIND_TIMER, timer, state, LINUX_O_RDONLY | (flags & TFD_NONBLOCK));
    if (fd < 0) {
        shim_timer_close(state);
    }
    return fd;
}

int shim_timer_close(TimerState *state)
{
    CloseHandle(state->timer);
    HeapFree(GetProcessHeap(), 0, state);
    return 0;
}

static TimerState *timer_state(int fd)
{
    if (shim_fd_flags(fd) < 0) {
        return NULL;
    }
    if (shim_fd_kind(fd) != FD_KIND_TIMER) {
        shim_set_errno(LINUX_EINVAL);
        return NULL;
    }
    return (TimerState *)shim_fd_state(fd);
}

/* Arms the Win32 timer to fire once after `delay_100ns` (at least one tick); a read re-arms it for the next period. */
static void arm(TimerState *state, int64_t delay_100ns)
{
    LARGE_INTEGER due;
    due.QuadPart = -(delay_100ns > 0 ? delay_100ns : 1);
    SetWaitableTimer(state->timer, &due, 0, NULL, NULL, FALSE);
}

static void disarm(TimerState *state)
{
    LARGE_INTEGER never;
    never.QuadPart = NEVER_DUE_100NS;
    SetWaitableTimer(state->timer, &never, 0, NULL, NULL, FALSE);
    state->next_expiry_100ns = 0;
    state->interval_100ns = 0;
}

LIBC_FN int sysv_timerfd_settime(int fd, int flags, const ITimerSpec *value, ITimerSpec *old)
{
    TimerState *state = timer_state(fd);
    if (state == NULL) {
        return -1;
    }
    if (old != NULL) {
        extern LIBC_FN int sysv_timerfd_gettime(int, ITimerSpec *);
        sysv_timerfd_gettime(fd, old);
    }
    int64_t first = to_100ns(value->value_seconds, value->value_nanoseconds);
    if (first == 0) {
        disarm(state);
        return 0;
    }
    int64_t now = now_100ns(state->clock);
    int64_t delay = (flags & TFD_TIMER_ABSTIME) ? first - now : first;
    state->interval_100ns = to_100ns(value->interval_seconds, value->interval_nanoseconds);
    state->next_expiry_100ns = now + delay;
    arm(state, delay);
    return 0;
}

LIBC_FN int sysv_timerfd_gettime(int fd, ITimerSpec *out)
{
    TimerState *state = timer_state(fd);
    if (state == NULL) {
        return -1;
    }
    int64_t remaining = state->next_expiry_100ns == 0 ? 0 : state->next_expiry_100ns - now_100ns(state->clock);
    remaining = remaining < 0 ? 0 : remaining;
    *out = (ITimerSpec){.interval_seconds = state->interval_100ns / UNITS_PER_SECOND,
                        .interval_nanoseconds = (state->interval_100ns % UNITS_PER_SECOND) * NANOSECONDS_PER_100NS,
                        .value_seconds = remaining / UNITS_PER_SECOND,
                        .value_nanoseconds = (remaining % UNITS_PER_SECOND) * NANOSECONDS_PER_100NS};
    return 0;
}

/* Waits until the timer has expired. Returns 0, or -1 with EAGAIN when the descriptor is non-blocking and it has not. */
static int wait_for_expiry(int fd, TimerState *state)
{
    while (state->next_expiry_100ns == 0 || WaitForSingleObject(state->timer, 0) != WAIT_OBJECT_0) {
        if ((shim_fd_flags(fd) & LINUX_O_NONBLOCK) != 0) {
            return shim_fail(LINUX_EAGAIN);
        }
        WaitForSingleObject(state->timer, WAIT_SLICE_MS);
    }
    return 0;
}

/* The periods that have passed since the expiry time (at least one); re-arms a periodic timer, disarms a one-shot. */
static uint64_t take_expirations(TimerState *state)
{
    uint64_t expirations = 1;
    int64_t now = now_100ns(state->clock);
    if (state->interval_100ns > 0) {
        uint64_t late = now > state->next_expiry_100ns ? (uint64_t)(now - state->next_expiry_100ns) : 0;
        expirations += late / (uint64_t)state->interval_100ns;
        state->next_expiry_100ns += (int64_t)expirations * state->interval_100ns;
        arm(state, state->next_expiry_100ns - now);
    } else {
        disarm(state);
    }
    return expirations;
}

int64_t shim_timer_read(int fd, void *buffer, size_t count)
{
    TimerState *state = timer_state(fd);
    if (state == NULL) {
        return -1;
    }
    if (count < sizeof(uint64_t)) {
        return shim_fail(LINUX_EINVAL);
    }
    if (wait_for_expiry(fd, state) != 0) {
        return -1;
    }
    *(uint64_t *)buffer = take_expirations(state);
    return (int64_t)sizeof(uint64_t);
}
