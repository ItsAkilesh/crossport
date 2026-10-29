/*
 * File: shim/fortify.c
 * Purpose: The _FORTIFY_SOURCE variants (__strcpy_chk, __read_chk, __poll_chk, ...) that distribution compilers emit
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Each checks the destination size the compiler supplied and aborts the way glibc does when the call
 * would overflow it, then does the plain operation. Variants that pre-date this file are in mem.c,
 * printf_api.c, scan.c, stdio.c and setjmp.c.
 */

#include "libc_abi.h"

#define SELECT_FD_LIMIT 1024
#define SELECT_WORD_BITS 64
#define POLLFD_BYTES 8

LIBC_FN NORETURN void sysv___chk_fail(void);
LIBC_FN size_t sysv_strlen(const char *text);
LIBC_FN char *sysv_strcpy(char *destination, const char *source);
LIBC_FN char *sysv_strncpy(char *destination, const char *source, size_t count);
LIBC_FN char *sysv_strcat(char *destination, const char *source);
LIBC_FN char *sysv_strncat(char *destination, const char *source, size_t count);
LIBC_FN char *sysv_stpcpy(char *destination, const char *source);
LIBC_FN int64_t sysv_read(int fd, void *buffer, size_t count);
LIBC_FN int64_t sysv_pread64(int fd, void *buffer, size_t count, int64_t offset);
LIBC_FN int sysv_poll(void *fds, uint64_t count, int timeout_ms);
LIBC_FN char *sysv_getcwd(char *buffer, size_t size);
LIBC_FN char *sysv_realpath(const char *path, char *resolved);
LIBC_FN int64_t sysv_readlink(const char *path, char *buffer, size_t size);
LIBC_FN char *sysv_fgets(char *buffer, int size, void *stream);

LIBC_FN char *sysv___strcpy_chk(char *destination, const char *source, size_t size)
{
    if (sysv_strlen(source) >= size) {
        sysv___chk_fail();
    }
    return sysv_strcpy(destination, source);
}

LIBC_FN char *sysv___stpcpy_chk(char *destination, const char *source, size_t size)
{
    if (sysv_strlen(source) >= size) {
        sysv___chk_fail();
    }
    return sysv_stpcpy(destination, source);
}

LIBC_FN char *sysv___strncpy_chk(char *destination, const char *source, size_t count, size_t size)
{
    if (count > size) {
        sysv___chk_fail();
    }
    return sysv_strncpy(destination, source, count);
}

LIBC_FN char *sysv___strcat_chk(char *destination, const char *source, size_t size)
{
    if (sysv_strlen(destination) + sysv_strlen(source) >= size) {
        sysv___chk_fail();
    }
    return sysv_strcat(destination, source);
}

LIBC_FN char *sysv___strncat_chk(char *destination, const char *source, size_t count, size_t size)
{
    size_t appended = sysv_strlen(source) < count ? sysv_strlen(source) : count;
    if (sysv_strlen(destination) + appended >= size) {
        sysv___chk_fail();
    }
    return sysv_strncat(destination, source, count);
}

LIBC_FN int64_t sysv___read_chk(int fd, void *buffer, size_t count, size_t size)
{
    if (count > size) {
        sysv___chk_fail();
    }
    return sysv_read(fd, buffer, count);
}

LIBC_FN int64_t sysv___pread64_chk(int fd, void *buffer, size_t count, int64_t offset, size_t size)
{
    if (count > size) {
        sysv___chk_fail();
    }
    return sysv_pread64(fd, buffer, count, offset);
}

LIBC_FN int sysv___poll_chk(void *fds, uint64_t count, int timeout_ms, size_t size)
{
    if (count * POLLFD_BYTES > size) {
        sysv___chk_fail();
    }
    return sysv_poll(fds, count, timeout_ms);
}

/* The word index of a descriptor in an fd_set, checked against the set's capacity. */
LIBC_FN int64_t sysv___fdelt_chk(int64_t fd)
{
    if (fd < 0 || fd >= SELECT_FD_LIMIT) {
        sysv___chk_fail();
    }
    return fd / SELECT_WORD_BITS;
}

LIBC_FN char *sysv___getcwd_chk(char *buffer, size_t size, size_t buffer_size)
{
    if (size > buffer_size) {
        sysv___chk_fail();
    }
    return sysv_getcwd(buffer, size);
}

LIBC_FN char *sysv___realpath_chk(const char *path, char *resolved, size_t resolved_size)
{
    if (resolved != NULL && resolved_size < 4096) {
        sysv___chk_fail();
    }
    return sysv_realpath(path, resolved);
}

LIBC_FN int64_t sysv___readlink_chk(const char *path, char *buffer, size_t count, size_t size)
{
    if (count > size) {
        sysv___chk_fail();
    }
    return sysv_readlink(path, buffer, count);
}

LIBC_FN char *sysv___fgets_chk(char *buffer, size_t size, int count, void *stream)
{
    if ((size_t)count > size) {
        sysv___chk_fail();
    }
    return sysv_fgets(buffer, count, stream);
}
