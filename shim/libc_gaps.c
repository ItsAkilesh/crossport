/*
 * File: shim/libc_gaps.c
 * Purpose: Small C library functions the compatibility survey found missing: tmpfile, mkdtemp, mkstemps, canonicalize_file_name, the
 *          64-bit glob names, wcswidth, rpmatch, stpncpy and its fortified form, __recv_chk, CPU-set allocation, IPv6 address constants,
 *          sbrk, pselect, and refusals for the kernel-module and mount calls
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Every function follows glibc. The refusals (module loading, the new mount API) return ENOSYS or EPERM: the calls cannot work in a
 * Windows process, and a program that asks for them is told so rather than failing to start.
 */

#include <string.h>

#include "fd.h"
#include "libc_abi.h"
#include "stdio_internal.h"

#define TEMPLATE_LENGTH 6
#define TEMP_NAME_ATTEMPTS 100
#define TEMP_FILE_MODE 0600
#define CPU_SET_BITS_PER_BYTE 8
#define EXIT_AFTER_FAILED_NAME 0

LIBC_FN int sysv_mkstemp64(char *pattern);
LIBC_FN int sysv_mkdir(const char *path, unsigned mode);
LIBC_FN int sysv_access(const char *path, int mode);
LIBC_FN char *sysv_realpath(const char *path, char *resolved);
LIBC_FN int sysv_glob(const char *pattern, int flags, void *error_function, void *result);
LIBC_FN void sysv_globfree(void *result);
LIBC_FN int64_t sysv_recv(int fd, void *buffer, size_t count, int flags);
LIBC_FN int sysv_select(int limit, void *reads, void *writes, void *errors, const void *timeout);
LIBC_FN int64_t sysv_wcwidth(uint32_t c);
LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN NORETURN void sysv___chk_fail(void);

LIBC_FN ShimFile *sysv_tmpfile64(void);

/* An anonymous temporary file: created, opened for update and unlinked at once (see stdio.c). */
LIBC_FN ShimFile *sysv_tmpfile(void)
{
    return sysv_tmpfile64();
}

/* Replaces the six trailing X characters (before `suffix_length` more characters) with a name that is not taken. */
static void fill_template(char *pattern, size_t suffix_length, unsigned attempt)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t length = strlen(pattern);
    char *start = pattern + length - suffix_length - TEMPLATE_LENGTH;
    uint32_t bits = (uint32_t)GetTickCount() ^ (attempt * 2654435761u) ^ GetCurrentProcessId();
    for (int i = 0; i < TEMPLATE_LENGTH; i++) {
        start[i] = alphabet[bits % (sizeof(alphabet) - 1)];
        bits = bits / (sizeof(alphabet) - 1) + attempt + (uint32_t)i;
    }
}

static int has_template(const char *pattern, size_t suffix_length)
{
    size_t length = strlen(pattern);
    if (length < suffix_length + TEMPLATE_LENGTH) {
        return 0;
    }
    return strncmp(pattern + length - suffix_length - TEMPLATE_LENGTH, "XXXXXX", TEMPLATE_LENGTH) == 0;
}

LIBC_FN char *sysv_mkdtemp(char *pattern)
{
    if (!has_template(pattern, 0)) {
        shim_set_errno(LINUX_EINVAL);
        return NULL;
    }
    for (unsigned attempt = 0; attempt < TEMP_NAME_ATTEMPTS; attempt++) {
        fill_template(pattern, 0, attempt);
        if (sysv_mkdir(pattern, 0700) == 0) {
            return pattern;
        }
        if (*shim_errno_ptr() != LINUX_EEXIST) {
            return NULL;
        }
    }
    return NULL;
}

/* mkstemps(3): the template is followed by `suffix_length` fixed characters. The file is created with its full name, exclusively. */
LIBC_FN int sysv_mkstemps(char *pattern, int suffix_length)
{
    if (suffix_length < 0 || !has_template(pattern, (size_t)suffix_length)) {
        return shim_fail(LINUX_EINVAL);
    }
    for (unsigned attempt = 0; attempt < TEMP_NAME_ATTEMPTS; attempt++) {
        fill_template(pattern, (size_t)suffix_length, attempt);
        int fd = shim_fd_open(pattern, LINUX_O_RDWR | LINUX_O_CREAT | LINUX_O_EXCL, TEMP_FILE_MODE);
        if (fd >= 0 || *shim_errno_ptr() != LINUX_EEXIST) {
            return fd;
        }
    }
    return shim_fail(LINUX_EEXIST);
}

LIBC_FN int sysv_mkstemps64(char *pattern, int suffix_length)
{
    return sysv_mkstemps(pattern, suffix_length);
}

LIBC_FN char *sysv_canonicalize_file_name(const char *path)
{
    return sysv_realpath(path, NULL);
}

LIBC_FN int sysv_glob64(const char *pattern, int flags, void *error_function, void *result)
{
    return sysv_glob(pattern, flags, error_function, result);
}

LIBC_FN void sysv_globfree64(void *result)
{
    sysv_globfree(result);
}

LIBC_FN int64_t sysv_wcswidth(const uint32_t *text, size_t count)
{
    int64_t total = 0;
    for (size_t i = 0; i < count && text[i] != 0; i++) {
        int64_t width = sysv_wcwidth(text[i]);
        if (width < 0) {
            return -1;
        }
        total += width;
    }
    return total;
}

/* rpmatch(3) for the C locale: 1 for an answer starting with y or Y, 0 for n or N, -1 otherwise. */
LIBC_FN int sysv_rpmatch(const char *answer)
{
    if (answer[0] == 'y' || answer[0] == 'Y') {
        return 1;
    }
    return answer[0] == 'n' || answer[0] == 'N' ? 0 : -1;
}

LIBC_FN char *sysv_stpncpy(char *destination, const char *source, size_t count);

LIBC_FN char *sysv___stpncpy_chk(char *destination, const char *source, size_t count, size_t destination_size)
{
    if (count > destination_size) {
        sysv___chk_fail();
    }
    return sysv_stpncpy(destination, source, count);
}

LIBC_FN int64_t sysv___recv_chk(int fd, void *buffer, size_t count, size_t buffer_size, int flags)
{
    if (count > buffer_size) {
        sysv___chk_fail();
    }
    return sysv_recv(fd, buffer, count, flags);
}

LIBC_FN void *sysv___sched_cpualloc(size_t count)
{
    size_t bytes = (count + CPU_SET_BITS_PER_BYTE - 1) / CPU_SET_BITS_PER_BYTE;
    bytes = (bytes + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1);
    void *set = sysv_malloc(bytes);
    if (set != NULL) {
        memset(set, 0, bytes);
    }
    return set;
}

LIBC_FN void sysv___sched_cpufree(void *set)
{
    sysv_free(set);
}

/* pselect(2): the signal mask is ignored (the shim blocks no signals); the timeout is a timespec. */
LIBC_FN int sysv_pselect(int limit, void *reads, void *writes, void *errors, const int64_t *timeout, const void *mask)
{
    (void)mask;
    int64_t microseconds[2];
    if (timeout != NULL) {
        microseconds[0] = timeout[0];
        microseconds[1] = timeout[1] / 1000;
    }
    return sysv_select(limit, reads, writes, errors, timeout != NULL ? microseconds : NULL);
}

/* The program break cannot be moved: callers fall back to mmap or malloc. */
LIBC_FN void *sysv_sbrk(int64_t increment)
{
    if (increment == 0) {
        return NULL;
    }
    shim_set_errno(LINUX_ENOMEM);
    return (void *)(intptr_t)-1;
}

LIBC_DATA(uint8_t, in6addr_any[16]);

LIBC_DATA(uint8_t, in6addr_loopback[16]) = {[15] = 1};

LIBC_DATA(int, __daylight);

/* The kernel-module and mount calls need privileges and a Linux kernel. */
LIBC_FN int sysv_init_module(void *image, unsigned long length, const char *parameters)
{
    (void)image;
    (void)length;
    (void)parameters;
    return shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_delete_module(const char *name, int flags)
{
    (void)name;
    (void)flags;
    return shim_fail(LINUX_EPERM);
}

LIBC_FN int sysv_fsopen(const char *name, unsigned flags)
{
    (void)name;
    (void)flags;
    return shim_fail(LINUX_ENOSYS);
}

LIBC_FN int sysv_fsconfig(int fd, unsigned command, const char *key, const void *value, int auxiliary)
{
    (void)fd;
    (void)command;
    (void)key;
    (void)value;
    (void)auxiliary;
    return shim_fail(LINUX_ENOSYS);
}

LIBC_FN int sysv_fsmount(int fd, unsigned flags, unsigned attributes)
{
    (void)fd;
    (void)flags;
    (void)attributes;
    return shim_fail(LINUX_ENOSYS);
}

/* malloc_info(3) writes an XML summary of the heap; an empty one is a valid answer. */
LIBC_FN int sysv_malloc_info(int options, ShimFile *stream)
{
    (void)options;
    (void)stream;
    return 0;
}
