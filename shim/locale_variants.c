/*
 * File: shim/locale_variants.c
 * Purpose: The *_l variants of the locale-sensitive functions, strxfrm, wcscoll and wcsftime
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN size_t sysv_strftime(char *out, size_t size, const char *format, const void *tm);
LIBC_FN double sysv_strtod(const char *text, char **end);
LIBC_FN float sysv_strtof(const char *text, char **end);
LIBC_FN long double sysv_strtold(const char *text, char **end);
LIBC_FN int sysv_strcoll(const char *first, const char *second);
LIBC_FN size_t sysv_mbstowcs(int32_t *out, const char *source, size_t limit);
LIBC_FN size_t sysv_wcstombs(char *out, const int32_t *source, size_t limit);
LIBC_FN uint32_t sysv_towlower(uint32_t c);
LIBC_FN uint32_t sysv_towupper(uint32_t c);
LIBC_FN uint64_t sysv_wctype(const char *name);
LIBC_FN int sysv_iswctype(uint32_t c, uint64_t kind);
LIBC_FN int sysv_wcscmp(const int32_t *first, const int32_t *second);
LIBC_FN size_t sysv_wcslen(const int32_t *text);

LIBC_FN size_t sysv_strxfrm(char *to, const char *from, size_t size)
{
    size_t length = strlen(from);
    if (size > 0) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(to, from, copied);
        to[copied] = '\0';
    }
    return length;
}

LIBC_FN int sysv___strcoll_l(const char *first, const char *second, void *locale)
{
    (void)locale;
    return sysv_strcoll(first, second);
}

LIBC_FN size_t sysv___strxfrm_l(char *to, const char *from, size_t size, void *locale)
{
    (void)locale;
    return sysv_strxfrm(to, from, size);
}

LIBC_FN size_t sysv___strftime_l(char *out, size_t size, const char *format, const void *tm, void *locale)
{
    (void)locale;
    return sysv_strftime(out, size, format, tm);
}

LIBC_FN double sysv___strtod_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtod(text, end);
}

LIBC_FN float sysv___strtof_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtof(text, end);
}

LIBC_FN long double sysv___strtold_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtold(text, end);
}

LIBC_FN uint32_t sysv___towlower_l(uint32_t c, void *locale)
{
    (void)locale;
    return sysv_towlower(c);
}

LIBC_FN uint32_t sysv___towupper_l(uint32_t c, void *locale)
{
    (void)locale;
    return sysv_towupper(c);
}

LIBC_FN uint64_t sysv___wctype_l(const char *name, void *locale)
{
    (void)locale;
    return sysv_wctype(name);
}

LIBC_FN int sysv___iswctype_l(uint32_t c, uint64_t kind, void *locale)
{
    (void)locale;
    return sysv_iswctype(c, kind);
}

LIBC_FN int sysv_wcscoll(const int32_t *first, const int32_t *second)
{
    return sysv_wcscmp(first, second);
}

LIBC_FN int sysv___wcscoll_l(const int32_t *first, const int32_t *second, void *locale)
{
    (void)locale;
    return sysv_wcscmp(first, second);
}

LIBC_FN size_t sysv_wcsxfrm(int32_t *to, const int32_t *from, size_t size)
{
    size_t length = sysv_wcslen(from);
    if (size > 0) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(to, from, copied * sizeof(int32_t));
        to[copied] = 0;
    }
    return length;
}

LIBC_FN size_t sysv___wcsxfrm_l(int32_t *to, const int32_t *from, size_t size, void *locale)
{
    (void)locale;
    return sysv_wcsxfrm(to, from, size);
}

/* strftime on the narrow form of the format, with the result widened again. */
LIBC_FN size_t sysv_wcsftime(int32_t *out, size_t size, const int32_t *format, const void *tm)
{
    char narrow_format[256];
    char narrow_out[512];
    if (sysv_wcstombs(narrow_format, format, sizeof(narrow_format)) >= sizeof(narrow_format)) {
        return 0;
    }
    size_t length = sysv_strftime(narrow_out, sizeof(narrow_out), narrow_format, tm);
    if (length == 0 || length + 1 > size) {
        return 0;
    }
    narrow_out[length] = '\0';
    return sysv_mbstowcs(out, narrow_out, size);
}

LIBC_FN size_t sysv___wcsftime_l(int32_t *out, size_t size, const int32_t *format, const void *tm, void *locale)
{
    (void)locale;
    return sysv_wcsftime(out, size, format, tm);
}

LIBC_FN double sysv_strtod_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtod(text, end);
}

LIBC_FN float sysv_strtof_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtof(text, end);
}

LIBC_FN long double sysv_strtold_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtold(text, end);
}
