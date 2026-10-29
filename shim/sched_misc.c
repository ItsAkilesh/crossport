/*
 * File: shim/sched_misc.c
 * Purpose: Signal set queries, pthread attribute getters, spawn attributes and CPU counting
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define SIGSET_BYTES 128
#define SIGRTMAX_VALUE 64
#define STACK_DEFAULT_BYTES (8u * 1024 * 1024)

LIBC_FN int sysv_sigdelset(void *set, int signal_number)
{
    if (signal_number < 1 || signal_number > SIGRTMAX_VALUE) {
        return shim_fail(LINUX_EINVAL);
    }
    ((uint8_t *)set)[(signal_number - 1) / 8] &= (uint8_t)~(1u << ((signal_number - 1) % 8));
    return 0;
}

LIBC_FN int sysv_sigismember(const void *set, int signal_number)
{
    if (signal_number < 1 || signal_number > SIGRTMAX_VALUE) {
        return shim_fail(LINUX_EINVAL);
    }
    return (((const uint8_t *)set)[(signal_number - 1) / 8] >> ((signal_number - 1) % 8)) & 1;
}

LIBC_FN int sysv_sigisemptyset(const void *set)
{
    for (size_t i = 0; i < SIGSET_BYTES; i++) {
        if (((const uint8_t *)set)[i] != 0) {
            return 0;
        }
    }
    return 1;
}

LIBC_FN int sysv___sched_cpucount(size_t size, const uint8_t *set)
{
    int count = 0;
    for (size_t i = 0; i < size; i++) {
        count += __builtin_popcount(set[i]);
    }
    return count;
}

LIBC_FN int sysv_pthread_kill(void *thread, int signal_number)
{
    (void)thread;
    return signal_number == 0 ? 0 : LINUX_ENOSYS;
}

LIBC_FN int sysv_pthread_attr_getstacksize(const void *attributes, size_t *size)
{
    (void)attributes;
    *size = STACK_DEFAULT_BYTES;
    return 0;
}

LIBC_FN int sysv_pthread_attr_getschedpolicy(const void *attributes, int *policy)
{
    (void)attributes;
    *policy = 0;
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_setflags(void *attributes, int16_t flags)
{
    (void)attributes;
    (void)flags;
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_setsigdefault(void *attributes, const void *set)
{
    (void)attributes;
    (void)set;
    return 0;
}
