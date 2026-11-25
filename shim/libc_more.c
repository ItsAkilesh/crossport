/*
 * File: shim/libc_more.c
 * Purpose: pause, sigqueue, setregid, getservbyport_r and the fortified unlocked fread: small calls the compatibility survey found missing
 * Author: Akilesh
 * Date: 2026-11-24
 */

#include "libc_abi.h"
#include "stdio_internal.h"

#define FIXED_GID 1000u

LIBC_FN int sysv_getpid(void);
LIBC_FN int sysv_raise(int signal_number);
LIBC_FN size_t sysv_fread_unlocked(void *destination, size_t size, size_t count, ShimFile *file);
LIBC_FN NORETURN void sysv___chk_fail(void);

/* pause(2): waits for a signal. The shim delivers none asynchronously, so this waits until the process ends. */
LIBC_FN int sysv_pause(void)
{
    Sleep(INFINITE);
    return shim_fail(LINUX_EINTR);
}

/* sigqueue(3): a process can signal itself; the value is not passed on. */
LIBC_FN int sysv_sigqueue(int pid, int signal_number, uintptr_t value)
{
    (void)value;
    if (pid != sysv_getpid()) {
        return shim_fail(LINUX_ESRCH);
    }
    return signal_number == 0 ? 0 : sysv_raise(signal_number);
}

/* The process cannot change its group; asking for the one it has succeeds. */
LIBC_FN int sysv_setregid(unsigned real, unsigned effective)
{
    int is_allowed = (real == FIXED_GID || real == (unsigned)-1) && (effective == FIXED_GID || effective == (unsigned)-1);
    return is_allowed ? 0 : shim_fail(LINUX_EPERM);
}

/* The services database is empty, so no port has a name. */
LIBC_FN int sysv_getservbyport_r(int port, const char *protocol, void *result_buffer, char *buffer, size_t size, void **result)
{
    (void)port;
    (void)protocol;
    (void)result_buffer;
    (void)buffer;
    (void)size;
    *result = NULL;
    return LINUX_ENOENT;
}

LIBC_FN size_t sysv___fread_unlocked_chk(void *destination, size_t destination_size, size_t size, size_t count, ShimFile *file)
{
    if (size != 0 && count > destination_size / size) {
        sysv___chk_fail();
    }
    return sysv_fread_unlocked(destination, size, count, file);
}
