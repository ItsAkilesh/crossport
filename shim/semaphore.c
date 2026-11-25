/*
 * File: shim/semaphore.c
 * Purpose: POSIX unnamed semaphores (sem_init, sem_wait, sem_trywait, sem_timedwait, sem_post, sem_getvalue, sem_destroy) on Win32 semaphores
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * The survey found sem_init, sem_post and sem_wait missing from 5 programs. A sem_t is 32 bytes; the first 16 hold the Windows handle and a
 * count kept beside it, because Win32 cannot read a semaphore's count without changing it. Named semaphores (sem_open) are not provided.
 */

#include "libc_abi.h"

#define SEMAPHORE_MAXIMUM 0x7fffffff
#define FILETIME_TICKS_PER_MILLISECOND 10000ull
#define FILETIME_UNIX_EPOCH_TICKS 116444736000000000ull
#define NANOSECONDS_PER_MILLISECOND 1000000
#define MILLISECONDS_PER_SECOND 1000

typedef struct {
    HANDLE handle;
    volatile LONG count;
    uint32_t padding;
} Semaphore;

_Static_assert(sizeof(Semaphore) <= 32, "Semaphore must fit in glibc's sem_t");

LIBC_FN int sysv_sem_init(Semaphore *semaphore, int is_shared, unsigned value)
{
    (void)is_shared;
    if (value > SEMAPHORE_MAXIMUM) {
        return shim_fail(LINUX_EINVAL);
    }
    semaphore->handle = CreateSemaphoreW(NULL, (LONG)value, SEMAPHORE_MAXIMUM, NULL);
    semaphore->count = (LONG)value;
    return semaphore->handle != NULL ? 0 : shim_fail(LINUX_ENOMEM);
}

LIBC_FN int sysv_sem_destroy(Semaphore *semaphore)
{
    if (semaphore->handle != NULL) {
        CloseHandle(semaphore->handle);
        semaphore->handle = NULL;
    }
    return 0;
}

LIBC_FN int sysv_sem_post(Semaphore *semaphore)
{
    if (!ReleaseSemaphore(semaphore->handle, 1, NULL)) {
        return shim_fail(LINUX_EINVAL);
    }
    InterlockedIncrement(&semaphore->count);
    return 0;
}

/* Waits up to `wait_ms` and takes one unit; `failure` is the errno when the wait ends first. */
static int take(Semaphore *semaphore, DWORD wait_ms, int failure)
{
    if (WaitForSingleObject(semaphore->handle, wait_ms) != WAIT_OBJECT_0) {
        return shim_fail(failure);
    }
    InterlockedDecrement(&semaphore->count);
    return 0;
}

LIBC_FN int sysv_sem_wait(Semaphore *semaphore)
{
    return take(semaphore, INFINITE, LINUX_EINTR);
}

LIBC_FN int sysv_sem_trywait(Semaphore *semaphore)
{
    return take(semaphore, 0, LINUX_EAGAIN);
}

/* Milliseconds from now until an absolute CLOCK_REALTIME time (seconds, nanoseconds); 0 when it has passed. */
static DWORD milliseconds_until(const int64_t *deadline)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    uint64_t ticks = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    int64_t now_ms = (int64_t)((ticks - FILETIME_UNIX_EPOCH_TICKS) / FILETIME_TICKS_PER_MILLISECOND);
    int64_t left = deadline[0] * MILLISECONDS_PER_SECOND + deadline[1] / NANOSECONDS_PER_MILLISECOND - now_ms;
    return left <= 0 ? 0 : (left > 0xFFFFFFFEll ? 0xFFFFFFFEu : (DWORD)left);
}

LIBC_FN int sysv_sem_timedwait(Semaphore *semaphore, const int64_t *deadline)
{
    return take(semaphore, milliseconds_until(deadline), LINUX_ETIMEDOUT);
}

LIBC_FN int sysv_sem_getvalue(Semaphore *semaphore, int *value)
{
    *value = (int)semaphore->count;
    return 0;
}
