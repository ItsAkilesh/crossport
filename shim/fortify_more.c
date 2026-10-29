/*
 * File: shim/fortify_more.c
 * Purpose: More _FORTIFY_SOURCE entry points: __explicit_bzero_chk, __readlinkat_chk, __open_2 and the locale strtoll variants
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN int sysv_open(const char *path, int flags, int mode);
LIBC_FN NORETURN void sysv___chk_fail(void);
LIBC_FN int64_t sysv_readlink(const char *path, char *buffer, size_t size);

LIBC_FN int sysv___explicit_bzero_chk(void *buffer, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    SecureZeroMemory(buffer, count);
    return 0;
}

LIBC_FN int64_t sysv___readlinkat_chk(int dirfd, const char *path, char *buffer, size_t count, size_t capacity)
{
    (void)dirfd;
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_readlink(path, buffer, count);
}

LIBC_FN size_t sysv_mbstowcs(int32_t *out, const char *source, size_t limit);

LIBC_FN size_t sysv___mbstowcs_chk(int32_t *out, const char *source, size_t limit, size_t capacity)
{
    if (out != NULL && limit > capacity) {
        sysv___chk_fail();
    }
    return sysv_mbstowcs(out, source, limit);
}

/* wcsncpy copies at most `count` wide characters and pads with zeros up to `count`. */
LIBC_FN int32_t *sysv_wcsncpy(int32_t *to, const int32_t *from, size_t count)
{
    size_t i = 0;
    for (; i < count && from[i] != 0; i++) {
        to[i] = from[i];
    }
    for (; i < count; i++) {
        to[i] = 0;
    }
    return to;
}

LIBC_FN int32_t *sysv___wcsncpy_chk(int32_t *to, const int32_t *from, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_wcsncpy(to, from, count);
}

LIBC_FN int sysv___open_2(const char *path, int flags)
{
    return sysv_open(path, flags, 0);
}

LIBC_FN int sysv___open64_2(const char *path, int flags)
{
    return sysv_open(path, flags, 0);
}

LIBC_FN int64_t sysv___isoc23_strtoll_l(const char *text, char **end, int base, void *locale)
{
    (void)locale;
    extern LIBC_FN int64_t sysv_strtoll(const char *, char **, int);
    return sysv_strtoll(text, end, base);
}

LIBC_FN uint64_t sysv___isoc23_strtoull_l(const char *text, char **end, int base, void *locale)
{
    (void)locale;
    extern LIBC_FN uint64_t sysv_strtoull(const char *, char **, int);
    return sysv_strtoull(text, end, base);
}
