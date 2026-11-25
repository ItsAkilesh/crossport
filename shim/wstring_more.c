/*
 * File: shim/wstring_more.c
 * Purpose: The wide-character string functions the application sweep found missing: wcscat, wcsncat, wcsdup, wcspbrk, wcsspn,
 *          wcscspn, wcsstr, wcstok and fputws
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * wchar_t is 32 bits on Linux, so these work on int32_t. Each follows the C standard.
 */

#include "libc_abi.h"
#include "stdio_internal.h"

typedef int32_t Wide;

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN size_t sysv_wcslen(const Wide *text);
LIBC_FN int64_t sysv_fputwc(Wide wide, ShimFile *file);

static int contains(const Wide *set, Wide wide)
{
    for (; *set != 0; set++) {
        if (*set == wide) {
            return 1;
        }
    }
    return 0;
}

LIBC_FN Wide *sysv_wcscat(Wide *destination, const Wide *source)
{
    Wide *end = destination + sysv_wcslen(destination);
    while ((*end++ = *source++) != 0) {
    }
    return destination;
}

LIBC_FN Wide *sysv_wcsncat(Wide *destination, const Wide *source, size_t count)
{
    Wide *end = destination + sysv_wcslen(destination);
    for (size_t i = 0; i < count && source[i] != 0; i++) {
        *end++ = source[i];
    }
    *end = 0;
    return destination;
}

LIBC_FN Wide *sysv_wcsdup(const Wide *text)
{
    size_t bytes = (sysv_wcslen(text) + 1) * sizeof(Wide);
    Wide *copy = sysv_malloc(bytes);
    if (copy == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    __builtin_memcpy(copy, text, bytes);
    return copy;
}

LIBC_FN Wide *sysv_wcspbrk(const Wide *text, const Wide *set)
{
    for (; *text != 0; text++) {
        if (contains(set, *text)) {
            return (Wide *)text;
        }
    }
    return NULL;
}

LIBC_FN size_t sysv_wcsspn(const Wide *text, const Wide *set)
{
    size_t length = 0;
    while (text[length] != 0 && contains(set, text[length])) {
        length++;
    }
    return length;
}

LIBC_FN size_t sysv_wcscspn(const Wide *text, const Wide *set)
{
    size_t length = 0;
    while (text[length] != 0 && !contains(set, text[length])) {
        length++;
    }
    return length;
}

LIBC_FN Wide *sysv_wcsstr(const Wide *haystack, const Wide *needle)
{
    if (*needle == 0) {
        return (Wide *)haystack;
    }
    for (; *haystack != 0; haystack++) {
        size_t i = 0;
        while (needle[i] != 0 && haystack[i] == needle[i]) {
            i++;
        }
        if (needle[i] == 0) {
            return (Wide *)haystack;
        }
    }
    return NULL;
}

/* wcstok(3): splits on any of `delimiters`, remembering the position in `*save`. */
LIBC_FN Wide *sysv_wcstok(Wide *text, const Wide *delimiters, Wide **save)
{
    Wide *start = text != NULL ? text : *save;
    if (start == NULL) {
        return NULL;
    }
    start += sysv_wcsspn(start, delimiters);
    if (*start == 0) {
        *save = NULL;
        return NULL;
    }
    Wide *end = start + sysv_wcscspn(start, delimiters);
    if (*end != 0) {
        *end++ = 0;
    }
    *save = end;
    return start;
}

/* fputws(3): returns a non-negative value, or -1 when a character cannot be written. */
LIBC_FN int sysv_fputws(const Wide *text, ShimFile *file)
{
    for (; *text != 0; text++) {
        if (sysv_fputwc(*text, file) == -1) {
            return -1;
        }
    }
    return 0;
}
