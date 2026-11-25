/*
 * File: shim/timed_locks.c
 * Purpose: pthread_mutex_timedlock and pthread_rwlock_timedrdlock / timedwrlock: take a lock, giving up at an absolute time
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * The survey found these missing from 5 programs. Each retries the lock's try-form every millisecond until the CLOCK_REALTIME deadline;
 * coarse, but the waits that use them are for rare contention.
 */

#include "libc_abi.h"

#define RETRY_MILLISECONDS 1
#define FILETIME_TICKS_PER_MILLISECOND 10000ull
#define FILETIME_UNIX_EPOCH_TICKS 116444736000000000ull
#define NANOSECONDS_PER_MILLISECOND 1000000
#define MILLISECONDS_PER_SECOND 1000
#define BUSY_CODE 16
#define TIMED_OUT_CODE 110

typedef int (SYSV *TryLock)(void *lock);

SYSV int pthread_mutex_trylock(void *mutex);
LIBC_FN int sysv_pthread_rwlock_tryrdlock(void *lock);
LIBC_FN int sysv_pthread_rwlock_trywrlock(void *lock);

static int64_t now_milliseconds(void)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    uint64_t ticks = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return (int64_t)((ticks - FILETIME_UNIX_EPOCH_TICKS) / FILETIME_TICKS_PER_MILLISECOND);
}

/* Tries the lock until it is free or the absolute deadline (seconds, nanoseconds) passes. Returns 0, ETIMEDOUT, or the try-form's error. */
static int lock_until(TryLock try_lock, void *lock, const int64_t *deadline)
{
    int64_t deadline_ms = deadline[0] * MILLISECONDS_PER_SECOND + deadline[1] / NANOSECONDS_PER_MILLISECOND;
    for (;;) {
        int result = try_lock(lock);
        if (result != BUSY_CODE) {
            return result;
        }
        if (now_milliseconds() >= deadline_ms) {
            return TIMED_OUT_CODE;
        }
        Sleep(RETRY_MILLISECONDS);
    }
}

LIBC_FN int sysv_pthread_mutex_timedlock(void *mutex, const int64_t *deadline)
{
    return lock_until(pthread_mutex_trylock, mutex, deadline);
}

LIBC_FN int sysv_pthread_rwlock_timedrdlock(void *lock, const int64_t *deadline)
{
    return lock_until(sysv_pthread_rwlock_tryrdlock, lock, deadline);
}

LIBC_FN int sysv_pthread_rwlock_timedwrlock(void *lock, const int64_t *deadline)
{
    return lock_until(sysv_pthread_rwlock_trywrlock, lock, deadline);
}
