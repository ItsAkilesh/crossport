/*
 * File: shim/timers.c
 * Purpose: alarm, setitimer, clock_nanosleep and clock_settime: the real-time timer signal and the sleeping calls built on the clocks
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the compatibility survey (alarm blocks 64 programs of Ubuntu's /usr/bin). One real-time timer per process, as on Linux:
 * a Windows timer-queue timer raises SIGALRM when it expires. The handler runs on the timer's thread, so it must not use
 * thread-local storage; setting a flag, the usual use, is safe. The default action ends the process (signal.c).
 */

#include "libc_abi.h"

#define SIGNAL_ALARM 14
#define MILLISECONDS_PER_SECOND 1000u
#define MICROSECONDS_PER_MILLISECOND 1000
#define NANOSECONDS_PER_SECOND 1000000000
#define TIMER_ABSOLUTE 1
#define TIMER_REAL 0

typedef struct {
    int64_t seconds;
    int64_t microseconds;
} TimeVal;

typedef struct {
    TimeVal interval;
    TimeVal value;
} ITimerVal;

void shim_deliver_timer_signal(int signal_number);
LIBC_FN int sysv_clock_gettime(int clock, int64_t *out);
LIBC_FN int sysv_nanosleep(const int64_t *request, int64_t *remaining);

static SRWLOCK g_lock = SRWLOCK_INIT;
static HANDLE g_timer;
static ULONGLONG g_deadline_ms;
static DWORD g_period_ms;

static VOID CALLBACK fired(PVOID context, BOOLEAN is_timer)
{
    (void)context;
    (void)is_timer;
    if (g_period_ms == 0) {
        g_deadline_ms = 0;
    } else {
        g_deadline_ms = GetTickCount64() + g_period_ms;
    }
    shim_deliver_timer_signal(SIGNAL_ALARM);
}

/* Milliseconds until the timer fires, rounded up, or 0 when none is set. Caller holds the lock. */
static ULONGLONG remaining_ms(void)
{
    ULONGLONG now = GetTickCount64();
    return g_deadline_ms > now ? g_deadline_ms - now : 0;
}

/* Replaces the timer. A due time of 0 cancels it. Caller holds the lock. */
static void arm(DWORD due_ms, DWORD period_ms)
{
    if (g_timer != NULL) {
        DeleteTimerQueueTimer(NULL, g_timer, NULL);
        g_timer = NULL;
    }
    g_deadline_ms = 0;
    g_period_ms = period_ms;
    if (due_ms == 0) {
        return;
    }
    g_deadline_ms = GetTickCount64() + due_ms;
    if (!CreateTimerQueueTimer(&g_timer, NULL, fired, NULL, due_ms, period_ms, WT_EXECUTEDEFAULT)) {
        g_timer = NULL;
        g_deadline_ms = 0;
    }
}

LIBC_FN unsigned sysv_alarm(unsigned seconds)
{
    AcquireSRWLockExclusive(&g_lock);
    ULONGLONG left = remaining_ms();
    uint64_t wanted_ms = (uint64_t)seconds * MILLISECONDS_PER_SECOND;
    arm(wanted_ms > 0xFFFFFFFEu ? 0xFFFFFFFEu : (DWORD)wanted_ms, 0);
    ReleaseSRWLockExclusive(&g_lock);
    return (unsigned)((left + MILLISECONDS_PER_SECOND - 1) / MILLISECONDS_PER_SECOND);
}

static DWORD to_milliseconds(const TimeVal *time)
{
    uint64_t total = (uint64_t)time->seconds * MILLISECONDS_PER_SECOND + (uint64_t)(time->microseconds + MICROSECONDS_PER_MILLISECOND - 1) / MICROSECONDS_PER_MILLISECOND;
    return total > 0xFFFFFFFEu ? 0xFFFFFFFEu : (DWORD)total;
}

static void from_milliseconds(TimeVal *out, ULONGLONG milliseconds)
{
    out->seconds = (int64_t)(milliseconds / MILLISECONDS_PER_SECOND);
    out->microseconds = (int64_t)(milliseconds % MILLISECONDS_PER_SECOND) * MICROSECONDS_PER_MILLISECOND;
}

/* Only the real-time timer exists; the virtual and profiling timers are refused. */
LIBC_FN int sysv_setitimer(int which, const ITimerVal *wanted, ITimerVal *previous)
{
    if (which != TIMER_REAL) {
        return shim_fail(LINUX_EINVAL);
    }
    AcquireSRWLockExclusive(&g_lock);
    if (previous != NULL) {
        from_milliseconds(&previous->value, remaining_ms());
        from_milliseconds(&previous->interval, g_period_ms);
    }
    if (wanted != NULL) {
        arm(to_milliseconds(&wanted->value), to_milliseconds(&wanted->interval));
    }
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}

LIBC_FN int sysv_getitimer(int which, ITimerVal *current)
{
    if (which != TIMER_REAL) {
        return shim_fail(LINUX_EINVAL);
    }
    AcquireSRWLockShared(&g_lock);
    from_milliseconds(&current->value, remaining_ms());
    from_milliseconds(&current->interval, g_period_ms);
    ReleaseSRWLockShared(&g_lock);
    return 0;
}

/* Returns the error number (not -1): clock_nanosleep reports through its return value. */
LIBC_FN int sysv_clock_nanosleep(int clock, int flags, const int64_t *request, int64_t *remaining)
{
    int64_t relative[2] = {request[0], request[1]};
    if (flags & TIMER_ABSOLUTE) {
        int64_t now[2];
        if (sysv_clock_gettime(clock, now) != 0) {
            return LINUX_EINVAL;
        }
        int64_t seconds = request[0] - now[0];
        int64_t nanoseconds = request[1] - now[1];
        if (nanoseconds < 0) {
            nanoseconds += NANOSECONDS_PER_SECOND;
            seconds--;
        }
        if (seconds < 0) {
            return 0;
        }
        relative[0] = seconds;
        relative[1] = nanoseconds;
    }
    return sysv_nanosleep(relative, (flags & TIMER_ABSOLUTE) ? NULL : remaining) == 0 ? 0 : LINUX_EINVAL;
}

/* An unprivileged process may not set a clock. */
LIBC_FN int sysv_clock_settime(int clock, const int64_t *time)
{
    (void)clock;
    (void)time;
    return shim_fail(LINUX_EPERM);
}
