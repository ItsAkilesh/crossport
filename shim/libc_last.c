/*
 * File: shim/libc_last.c
 * Purpose: memccpy, posix_fallocate, gethostbyaddr and getservbyport: the next entries of the survey's list
 * Author: Akilesh
 * Date: 2026-11-24
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN int sysv_fstat(int fd, void *buffer);
LIBC_FN int sysv_ftruncate(int fd, int64_t length);

/* The size field of the Linux x86-64 struct stat sits 48 bytes in (see stat.c). */
#define STAT_SIZE_OFFSET 48
#define STAT_BUFFER_BYTES 144

/* Copies up to and including the first `stop` byte, or `count` bytes; returns the byte after the copied stop byte, or NULL. */
LIBC_FN void *sysv_memccpy(void *destination, const void *source, int stop, size_t count)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0; i < count; i++) {
        out[i] = in[i];
        if (in[i] == (uint8_t)stop) {
            return out + i + 1;
        }
    }
    return NULL;
}

/* posix_fallocate(3): the file is made at least offset + length bytes long. Returns an error number, not -1. */
LIBC_FN int sysv_posix_fallocate(int fd, int64_t offset, int64_t length)
{
    uint8_t stat[STAT_BUFFER_BYTES];
    if (offset < 0 || length <= 0) {
        return LINUX_EINVAL;
    }
    if (sysv_fstat(fd, stat) != 0) {
        return *shim_errno_ptr();
    }
    int64_t current;
    memcpy(&current, stat + STAT_SIZE_OFFSET, sizeof(current));
    if (current >= offset + length) {
        return 0;
    }
    return sysv_ftruncate(fd, offset + length) == 0 ? 0 : *shim_errno_ptr();
}

/* The hosts and services databases are empty. */
LIBC_FN void *sysv_gethostbyaddr(const void *address, unsigned length, int family)
{
    (void)address;
    (void)length;
    (void)family;
    shim_set_errno(LINUX_ENOENT);
    return NULL;
}

LIBC_FN void *sysv_getservbyport(int port, const char *protocol)
{
    (void)port;
    (void)protocol;
    return NULL;
}
