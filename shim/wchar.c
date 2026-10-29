/*
 * File: shim/wchar.c
 * Purpose: Multibyte and wide-character conversion (UTF-8 and 32-bit wchar_t) and the wide string and memory functions
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The shim's locale is always UTF-8, which is what Linux programs run in. glibc's own "C" locale refuses
 * bytes above 127, but a program can only observe that difference with invalid input.
 */

#include <string.h>

#include "libc_abi.h"

#define WEOF_VALUE 0xffffffffu
#define MAX_UTF8_BYTES 4
#define MAX_CODE_POINT 0x10ffffu
#define SURROGATE_FIRST 0xd800u
#define SURROGATE_LAST 0xdfffu
#define CONTINUATION_MASK 0xc0u
#define CONTINUATION_TAG 0x80u
#define BYTE_ERROR ((size_t)-1)
#define BYTE_INCOMPLETE ((size_t)-2)

typedef int32_t WChar;

/* mbstate_t is 8 bytes in glibc; only the shim reads this layout. */
typedef struct {
    uint32_t value;     /* code point bits gathered so far */
    uint32_t remaining; /* continuation bytes still expected */
} MbState;

_Static_assert(sizeof(MbState) <= 8, "MbState must fit in mbstate_t");

LIBC_FN size_t sysv_mbrtowc(WChar *out, const char *text, size_t count, MbState *state);

/* Bytes of the sequence introduced by `lead`, 0 for an invalid lead byte. */
static unsigned sequence_length(uint8_t lead)
{
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xc2 && lead <= 0xdf) {
        return 2;
    }
    if (lead >= 0xe0 && lead <= 0xef) {
        return 3;
    }
    return (lead >= 0xf0 && lead <= 0xf4) ? 4 : 0;
}

static uint32_t lead_bits(uint8_t lead, unsigned length)
{
    return length == 1 ? lead : (lead & (0xffu >> (length + 1)));
}

LIBC_FN size_t sysv_mbrtowc(WChar *out, const char *text, size_t count, MbState *state)
{
    static MbState internal;
    MbState *at = state != NULL ? state : &internal;
    if (text == NULL) {
        *at = (MbState){0};
        return 0;
    }
    size_t used = 0;
    if (at->remaining == 0) {
        if (count == 0) {
            return BYTE_INCOMPLETE;
        }
        unsigned length = sequence_length((uint8_t)text[0]);
        if (length == 0) {
            shim_set_errno(LINUX_EILSEQ);
            return BYTE_ERROR;
        }
        at->value = lead_bits((uint8_t)text[0], length);
        at->remaining = length - 1;
        used = 1;
    }
    while (at->remaining > 0 && used < count) {
        if (((uint8_t)text[used] & CONTINUATION_MASK) != CONTINUATION_TAG) {
            *at = (MbState){0};
            shim_set_errno(LINUX_EILSEQ);
            return BYTE_ERROR;
        }
        at->value = (at->value << 6) | ((uint8_t)text[used] & 0x3fu);
        at->remaining--;
        used++;
    }
    if (at->remaining > 0) {
        return BYTE_INCOMPLETE;
    }
    uint32_t value = at->value;
    *at = (MbState){0};
    if (value > MAX_CODE_POINT || (value >= SURROGATE_FIRST && value <= SURROGATE_LAST)) {
        shim_set_errno(LINUX_EILSEQ);
        return BYTE_ERROR;
    }
    if (out != NULL) {
        *out = (WChar)value;
    }
    return value == 0 ? 0 : used;
}

LIBC_FN size_t sysv_wcrtomb(char *out, WChar wide, MbState *state)
{
    (void)state;
    uint32_t value = (uint32_t)wide;
    if (out == NULL) {
        return 1;
    }
    if (value > MAX_CODE_POINT || (value >= SURROGATE_FIRST && value <= SURROGATE_LAST)) {
        shim_set_errno(LINUX_EILSEQ);
        return BYTE_ERROR;
    }
    if (value < 0x80) {
        out[0] = (char)value;
        return 1;
    }
    unsigned length = value < 0x800 ? 2 : (value < 0x10000 ? 3 : 4);
    for (unsigned i = length - 1; i > 0; i--) {
        out[i] = (char)(CONTINUATION_TAG | (value & 0x3fu));
        value >>= 6;
    }
    out[0] = (char)((0xf00u >> length) | value);
    return length;
}

LIBC_FN uint32_t sysv_btowc(int byte)
{
    return (byte >= 0 && byte < 0x80) ? (uint32_t)byte : WEOF_VALUE;
}

LIBC_FN int sysv_wctob(uint32_t wide)
{
    return wide < 0x80 ? (int)wide : -1;
}

LIBC_FN size_t sysv_mbrlen(const char *text, size_t count, MbState *state)
{
    return sysv_mbrtowc(NULL, text, count, state);
}

LIBC_FN int sysv_mbtowc(WChar *out, const char *text, size_t count)
{
    if (text == NULL) {
        return 0;
    }
    MbState state = {0};
    size_t used = sysv_mbrtowc(out, text, count, &state);
    return (used == BYTE_ERROR || used == BYTE_INCOMPLETE) ? -1 : (int)(text[0] == '\0' ? 0 : used);
}

LIBC_FN int sysv_mblen(const char *text, size_t count)
{
    return sysv_mbtowc(NULL, text, count);
}

LIBC_FN int sysv_wctomb(char *out, WChar wide)
{
    if (out == NULL) {
        return 0;
    }
    size_t length = sysv_wcrtomb(out, wide, NULL);
    return length == BYTE_ERROR ? -1 : (int)length;
}

/* Converts up to `limit` characters (or until the terminator); `*source` is advanced as glibc does. */
static size_t convert_multibyte(WChar *out, const char **source, size_t bytes, size_t limit, MbState *state)
{
    size_t produced = 0;
    const char *at = *source;
    while (out == NULL || produced < limit) {
        WChar wide;
        size_t used = bytes > 0 ? sysv_mbrtowc(&wide, at, bytes, state) : BYTE_INCOMPLETE;
        if (used == BYTE_ERROR) {
            *source = at;
            return BYTE_ERROR;
        }
        if (used == BYTE_INCOMPLETE) {
            break;
        }
        if (out != NULL) {
            out[produced] = wide;
        }
        if (wide == 0) {
            at = NULL;
            break;
        }
        produced++;
        at += used;
        bytes -= used;
    }
    if (out != NULL) {
        *source = at;
    }
    return produced;
}

LIBC_FN size_t sysv_mbsnrtowcs(WChar *out, const char **source, size_t bytes, size_t limit, MbState *state)
{
    return convert_multibyte(out, source, bytes, limit, state);
}

LIBC_FN size_t sysv_mbsrtowcs(WChar *out, const char **source, size_t limit, MbState *state)
{
    return convert_multibyte(out, source, strlen(*source) + 1, limit, state);
}

LIBC_FN size_t sysv_mbstowcs(WChar *out, const char *source, size_t limit)
{
    const char *at = source;
    return convert_multibyte(out, &at, strlen(source) + 1, limit, NULL);
}

LIBC_FN size_t sysv_wcsrtombs(char *out, const WChar **source, size_t limit, MbState *state)
{
    size_t produced = 0;
    const WChar *at = *source;
    for (;;) {
        char buffer[MAX_UTF8_BYTES];
        size_t length = sysv_wcrtomb(buffer, *at, state);
        if (length == BYTE_ERROR) {
            *source = at;
            return BYTE_ERROR;
        }
        if (out != NULL && produced + length > limit) {
            break;
        }
        if (out != NULL) {
            memcpy(out + produced, buffer, length);
        }
        if (*at == 0) {
            *source = NULL;
            return produced;
        }
        produced += length;
        at++;
    }
    *source = at;
    return produced;
}

LIBC_FN size_t sysv_wcstombs(char *out, const WChar *source, size_t limit)
{
    const WChar *at = source;
    return sysv_wcsrtombs(out, &at, limit, NULL);
}

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

/* Fortify variants: the destination capacity is in elements. */
LIBC_FN NORETURN void sysv___chk_fail(void);

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

LIBC_FN size_t sysv___mbsrtowcs_chk(WChar *out, const char **source, size_t limit, MbState *state, size_t capacity)
{
    if (out != NULL && limit > capacity) {
        sysv___chk_fail();
    }
    return sysv_mbsrtowcs(out, source, limit, state);
}

LIBC_FN size_t sysv___mbsnrtowcs_chk(WChar *out, const char **source, size_t bytes, size_t limit, MbState *state,
                                     size_t capacity)
{
    if (out != NULL && limit > capacity) {
        sysv___chk_fail();
    }
    return sysv_mbsnrtowcs(out, source, bytes, limit, state);
}

/* Like wcsrtombs, but stops after `count` wide characters. */
LIBC_FN size_t sysv_wcsnrtombs(char *out, const WChar **source, size_t count, size_t limit, MbState *state)
{
    size_t available = 0;
    while (available < count && (*source)[available] != 0) {
        available++;
    }
    WChar bounded[available + 1];
    memcpy(bounded, *source, available * sizeof(WChar));
    bounded[available] = 0;
    const WChar *at = bounded;
    size_t produced = sysv_wcsrtombs(out, &at, limit, state);
    if (out != NULL) {
        *source = at == NULL ? NULL : *source + (at - bounded);
    }
    return produced;
}
