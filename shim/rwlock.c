/*
 * File: shim/rwlock.c
 * Purpose: pthread read-write locks on Win32 slim reader/writer locks
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The layout overlays glibc's 56-byte pthread_rwlock_t so that its all-zero static initialiser is a valid,
 * unlocked lock. An SRW lock must be released with the call matching how it was taken, so a writer flag
 * (only written while the lock is held exclusively) tells unlock which one to use.
 */

#include "libc_abi.h"

typedef struct {
    SRWLOCK lock;
    volatile int32_t is_writer;
} RwLock;

_Static_assert(sizeof(RwLock) <= 56, "RwLock must fit in pthread_rwlock_t");

LIBC_FN int sysv_pthread_rwlock_init(RwLock *lock, const void *attributes)
{
    (void)attributes;
    InitializeSRWLock(&lock->lock);
    lock->is_writer = 0;
    return 0;
}

LIBC_FN int sysv_pthread_rwlock_destroy(RwLock *lock)
{
    (void)lock;
    return 0;
}

LIBC_FN int sysv_pthread_rwlock_rdlock(RwLock *lock)
{
    AcquireSRWLockShared(&lock->lock);
    return 0;
}

LIBC_FN int sysv_pthread_rwlock_tryrdlock(RwLock *lock)
{
    return TryAcquireSRWLockShared(&lock->lock) ? 0 : LINUX_EBUSY;
}

LIBC_FN int sysv_pthread_rwlock_wrlock(RwLock *lock)
{
    AcquireSRWLockExclusive(&lock->lock);
    lock->is_writer = 1;
    return 0;
}

LIBC_FN int sysv_pthread_rwlock_trywrlock(RwLock *lock)
{
    if (!TryAcquireSRWLockExclusive(&lock->lock)) {
        return LINUX_EBUSY;
    }
    lock->is_writer = 1;
    return 0;
}

LIBC_FN int sysv_pthread_rwlock_unlock(RwLock *lock)
{
    if (lock->is_writer) {
        lock->is_writer = 0;
        ReleaseSRWLockExclusive(&lock->lock);
    } else {
        ReleaseSRWLockShared(&lock->lock);
    }
    return 0;
}
