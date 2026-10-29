/*
 * File: shim/wstring.c
 * Purpose: The wide string and wide memory functions (wcslen, wmemcpy, wcscmp, ...)
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

typedef int32_t WChar;

/* mbstate_t is 8 bytes in glibc; only the shim reads this layout. */
typedef struct {
    uint32_t value;     /* code point bits gathered so far */
    uint32_t remaining; /* continuation bytes still expected */
} MbState;

_Static_assert(sizeof(MbState) <= 8, "MbState must fit in mbstate_t");

/* Fortify variants: the destination capacity is in elements. */
LIBC_FN NORETURN void sysv___chk_fail(void);

LIBC_FN size_t sysv_wcslen(const WChar *text)
{
    size_t length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

LIBC_FN WChar *sysv_wmemcpy(WChar *to, const WChar *from, size_t count)
{
    return (WChar *)memcpy(to, from, count * sizeof(WChar));
}

LIBC_FN WChar *sysv_wmemmove(WChar *to, const WChar *from, size_t count)
{
    return (WChar *)memmove(to, from, count * sizeof(WChar));
}

LIBC_FN WChar *sysv_wmemset(WChar *to, WChar value, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        to[i] = value;
    }
    return to;
}

LIBC_FN int sysv_wmemcmp(const WChar *first, const WChar *second, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (first[i] != second[i]) {
            return first[i] < second[i] ? -1 : 1;
        }
    }
    return 0;
}

LIBC_FN WChar *sysv_wmemchr(const WChar *text, WChar value, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (text[i] == value) {
            return (WChar *)&text[i];
        }
    }
    return NULL;
}

LIBC_FN int sysv_wcscmp(const WChar *first, const WChar *second)
{
    while (*first != 0 && *first == *second) {
        first++;
        second++;
    }
    return *first < *second ? -1 : (*first > *second ? 1 : 0);
}

LIBC_FN int sysv_wcsncmp(const WChar *first, const WChar *second, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (first[i] != second[i] || first[i] == 0) {
            return first[i] < second[i] ? -1 : (first[i] > second[i] ? 1 : 0);
        }
    }
    return 0;
}

LIBC_FN WChar *sysv_wcscpy(WChar *to, const WChar *from)
{
    size_t length = sysv_wcslen(from);
    memcpy(to, from, (length + 1) * sizeof(WChar));
    return to;
}

LIBC_FN WChar *sysv_wcschr(const WChar *text, WChar value)
{
    for (;; text++) {
        if (*text == value) {
            return (WChar *)text;
        }
        if (*text == 0) {
            return NULL;
        }
    }
}

LIBC_FN WChar *sysv_wcsrchr(const WChar *text, WChar value)
{
    const WChar *found = NULL;
    for (;; text++) {
        if (*text == value) {
            found = text;
        }
        if (*text == 0) {
            return (WChar *)found;
        }
    }
}

LIBC_FN WChar *sysv___wmemcpy_chk(WChar *to, const WChar *from, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_wmemcpy(to, from, count);
}

LIBC_FN WChar *sysv___wmemmove_chk(WChar *to, const WChar *from, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_wmemmove(to, from, count);
}

LIBC_FN WChar *sysv___wmemset_chk(WChar *to, WChar value, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_wmemset(to, value, count);
}
