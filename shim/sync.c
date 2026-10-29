/*
 * File: shim/sync.c
 * Purpose: pthread mutexes, condition variables and pthread_once on Win32 synchronisation primitives
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Layouts follow glibc's x86-64 ABI so static initialisers work unchanged: an
 * all-zero object is a valid normal mutex or condition variable, and the
 * recursive and error-checking initialisers set the kind word at offset 16.
 */

#include <stdint.h>

#include "internal.h"

#define EBUSY_CODE 16
#define EPERM_CODE 1
#define EINVAL_CODE 22
#define EDEADLK_CODE 35
#define ETIMEDOUT_CODE 110

enum MutexKind { KIND_NORMAL = 0, KIND_RECURSIVE = 1, KIND_ERRORCHECK = 2 };

/* Overlays glibc's 40-byte pthread_mutex_t: the zero-initialised SRWLOCK sits where __lock/__count are. */
typedef struct {
    SRWLOCK lock;
    uint32_t owner; /* Windows thread id; only tracked for non-normal kinds */
    uint32_t depth; /* recursion depth */
    int32_t kind;   /* glibc's __kind, offset 16 */
} Mutex;

_Static_assert(sizeof(Mutex) <= 40, "Mutex must fit in pthread_mutex_t");
_Static_assert(__builtin_offsetof(Mutex, kind) == 16, "kind must be where glibc's static initialisers put it");

/* Overlays the 48-byte pthread_cond_t. The clock a timed wait measures its deadline on is 0 (realtime) or 1 (monotonic). */
typedef struct {
    CONDITION_VARIABLE variable;
    int32_t clock;
} Cond;

typedef struct {
    int32_t clock;
} CondAttr;

enum { CLOCK_REALTIME_ID = 0, CLOCK_MONOTONIC_ID = 1 };

typedef struct {
    int32_t kind;
} MutexAttr;

typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} TimeSpec;

EXPORT SYSV int pthread_mutexattr_init(MutexAttr *attr)
{
    attr->kind = KIND_NORMAL;
    return 0;
}

EXPORT SYSV int pthread_mutexattr_destroy(MutexAttr *attr)
{
    (void)attr;
    return 0;
}

EXPORT SYSV int pthread_mutexattr_settype(MutexAttr *attr, int kind)
{
    if (kind < KIND_NORMAL || kind > KIND_ERRORCHECK) {
        return EINVAL_CODE;
    }
    attr->kind = kind;
    return 0;
}

EXPORT SYSV int pthread_mutex_init(Mutex *mutex, const MutexAttr *attr)
{
    InitializeSRWLock(&mutex->lock);
    mutex->owner = 0;
    mutex->depth = 0;
    mutex->kind = attr != NULL ? attr->kind : KIND_NORMAL;
    return 0;
}

EXPORT SYSV int pthread_mutex_destroy(Mutex *mutex)
{
    (void)mutex;
    return 0;
}

EXPORT SYSV int pthread_mutex_lock(Mutex *mutex)
{
    if (mutex->kind == KIND_NORMAL) {
        AcquireSRWLockExclusive(&mutex->lock);
        return 0;
    }
    uint32_t self = GetCurrentThreadId();
    if (mutex->owner == self) {
        if (mutex->kind == KIND_ERRORCHECK) {
            return EDEADLK_CODE;
        }
        mutex->depth++;
        return 0;
    }
    AcquireSRWLockExclusive(&mutex->lock);
    mutex->owner = self;
    mutex->depth = 1;
    return 0;
}

EXPORT SYSV int pthread_mutex_trylock(Mutex *mutex)
{
    uint32_t self = GetCurrentThreadId();
    if (mutex->kind == KIND_RECURSIVE && mutex->owner == self) {
        mutex->depth++;
        return 0;
    }
    if (!TryAcquireSRWLockExclusive(&mutex->lock)) {
        return EBUSY_CODE;
    }
    if (mutex->kind != KIND_NORMAL) {
        mutex->owner = self;
        mutex->depth = 1;
    }
    return 0;
}

EXPORT SYSV int pthread_mutex_unlock(Mutex *mutex)
{
    if (mutex->kind == KIND_NORMAL) {
        ReleaseSRWLockExclusive(&mutex->lock);
        return 0;
    }
    if (mutex->owner != GetCurrentThreadId()) {
        return EPERM_CODE;
    }
    if (--mutex->depth == 0) {
        mutex->owner = 0;
        ReleaseSRWLockExclusive(&mutex->lock);
    }
    return 0;
}

EXPORT SYSV int pthread_cond_init(Cond *cond, const CondAttr *attr)
{
    InitializeConditionVariable(&cond->variable);
    cond->clock = attr != NULL ? attr->clock : CLOCK_REALTIME_ID;
    return 0;
}

EXPORT SYSV int pthread_condattr_init(CondAttr *attr)
{
    attr->clock = CLOCK_REALTIME_ID;
    return 0;
}

EXPORT SYSV int pthread_condattr_destroy(CondAttr *attr)
{
    (void)attr;
    return 0;
}

EXPORT SYSV int pthread_condattr_setclock(CondAttr *attr, int clock)
{
    if (clock != CLOCK_REALTIME_ID && clock != CLOCK_MONOTONIC_ID) {
        return EINVAL_CODE;
    }
    attr->clock = clock;
    return 0;
}

EXPORT SYSV int pthread_condattr_getclock(const CondAttr *attr, int *clock)
{
    *clock = attr->clock;
    return 0;
}

EXPORT SYSV int pthread_cond_destroy(Cond *cond)
{
    (void)cond;
    return 0;
}

EXPORT SYSV int pthread_cond_signal(Cond *cond)
{
    WakeConditionVariable(&cond->variable);
    return 0;
}

EXPORT SYSV int pthread_cond_broadcast(Cond *cond)
{
    WakeAllConditionVariable(&cond->variable);
    return 0;
}

/* Waits on the condition, releasing the mutex's SRW lock; restores ownership state afterwards. */
static int wait_on(Cond *cond, Mutex *mutex, DWORD milliseconds)
{
    uint32_t saved_owner = mutex->owner;
    uint32_t saved_depth = mutex->depth;
    if (mutex->kind != KIND_NORMAL) {
        if (saved_owner != GetCurrentThreadId()) {
            return EPERM_CODE;
        }
        mutex->owner = 0;
        mutex->depth = 0;
    }
    BOOL woke = SleepConditionVariableSRW(&cond->variable, &mutex->lock, milliseconds, 0);
    if (mutex->kind != KIND_NORMAL) {
        mutex->owner = saved_owner;
        mutex->depth = saved_depth;
    }
    return woke ? 0 : ETIMEDOUT_CODE;
}

EXPORT SYSV int pthread_cond_wait(Cond *cond, Mutex *mutex)
{
    return wait_on(cond, mutex, INFINITE);
}

#define UNIX_EPOCH_AS_FILETIME 116444736000000000ull /* 100 ns ticks from 1601 to 1970 */
#define NANOSECONDS_PER_TICK 100ull
#define NANOSECONDS_PER_SECOND 1000000000ull
#define NANOSECONDS_PER_MILLISECOND 1000000ull

/* The time now on `clock` in nanoseconds: since the Unix epoch, or the monotonic performance counter. */
static uint64_t nanoseconds_now(int clock)
{
    if (clock == CLOCK_MONOTONIC_ID) {
        LARGE_INTEGER counter;
        LARGE_INTEGER frequency;
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);
        uint64_t whole = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
        uint64_t part = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
        return whole * NANOSECONDS_PER_SECOND + part * NANOSECONDS_PER_SECOND / (uint64_t)frequency.QuadPart;
    }
    FILETIME file_time;
    GetSystemTimePreciseAsFileTime(&file_time);
    uint64_t ticks = ((uint64_t)file_time.dwHighDateTime << 32) | file_time.dwLowDateTime;
    return (ticks - UNIX_EPOCH_AS_FILETIME) * NANOSECONDS_PER_TICK;
}

/* Milliseconds from now until the absolute deadline on `clock`, rounded up; zero if it has passed. */
static DWORD milliseconds_until(int clock, const TimeSpec *deadline)
{
    uint64_t now = nanoseconds_now(clock);
    uint64_t target = (uint64_t)deadline->seconds * NANOSECONDS_PER_SECOND + (uint64_t)deadline->nanoseconds;
    if (target <= now) {
        return 0;
    }
    uint64_t milliseconds = (target - now + NANOSECONDS_PER_MILLISECOND - 1) / NANOSECONDS_PER_MILLISECOND;
    return milliseconds >= INFINITE ? INFINITE - 1 : (DWORD)milliseconds;
}

EXPORT SYSV int pthread_cond_timedwait(Cond *cond, Mutex *mutex, const TimeSpec *deadline)
{
    if (deadline->nanoseconds < 0 || deadline->nanoseconds >= (int64_t)NANOSECONDS_PER_SECOND) {
        return EINVAL_CODE;
    }
    return wait_on(cond, mutex, milliseconds_until(cond->clock, deadline));
}

EXPORT SYSV int pthread_cond_clockwait(Cond *cond, Mutex *mutex, int clock, const TimeSpec *deadline)
{
    if (deadline->nanoseconds < 0 || deadline->nanoseconds >= (int64_t)NANOSECONDS_PER_SECOND ||
        (clock != CLOCK_REALTIME_ID && clock != CLOCK_MONOTONIC_ID)) {
        return EINVAL_CODE;
    }
    return wait_on(cond, mutex, milliseconds_until(clock, deadline));
}

/* pthread_once_t is one int: 0 = not started, 1 = running, 2 = done. */
enum OnceState { ONCE_NEW = 0, ONCE_RUNNING = 1, ONCE_DONE = 2 };

EXPORT SYSV int pthread_once(volatile LONG *once, void (SYSV *routine)(void))
{
    if (*once == ONCE_DONE) {
        return 0;
    }
    if (InterlockedCompareExchange(once, ONCE_RUNNING, ONCE_NEW) == ONCE_NEW) {
        routine();
        InterlockedExchange(once, ONCE_DONE);
        WakeByAddressAll((PVOID)once);
        return 0;
    }
    LONG running = ONCE_RUNNING;
    while (*once == ONCE_RUNNING) {
        WaitOnAddress(once, &running, sizeof(running), INFINITE);
    }
    return 0;
}
