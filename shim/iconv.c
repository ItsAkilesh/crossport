/*
 * File: shim/iconv.c
 * Purpose: iconv_open, iconv and iconv_close for UTF-8, ASCII, Latin-1, UTF-16 and UTF-32
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Every conversion decodes one code point from the source charset and encodes it in the target one.
 * Windows code pages are not used: the set is small, fixed and has the same behaviour on every host.
 */

#include <ctype.h>
#include <string.h>

#include "libc_abi.h"

#define NAME_MAX_LENGTH 64
#define ICONV_ERROR ((void *)-1)
#define MAX_CODE_POINT 0x10ffffu
#define SURROGATE_FIRST 0xd800u
#define SURROGATE_LAST 0xdfffu
#define REPLACEMENT_BYTE '?'

typedef enum { CS_ASCII, CS_LATIN1, CS_UTF8, CS_UTF16LE, CS_UTF16BE, CS_UTF32LE, CS_UTF32BE } Charset;

typedef struct {
    const char *name;
    Charset charset;
} CharsetName;

static const CharsetName NAMES[] = {
    {"UTF-8", CS_UTF8}, {"UTF8", CS_UTF8}, {"ASCII", CS_ASCII}, {"US-ASCII", CS_ASCII}, {"ANSI_X3.4-1968", CS_ASCII},
    {"ISO-8859-1", CS_LATIN1}, {"ISO8859-1", CS_LATIN1}, {"LATIN1", CS_LATIN1}, {"UTF-16LE", CS_UTF16LE},
    {"UTF-16", CS_UTF16LE}, {"UCS-2LE", CS_UTF16LE}, {"UTF-16BE", CS_UTF16BE}, {"UCS-2BE", CS_UTF16BE},
    {"UTF-32LE", CS_UTF32LE}, {"UTF-32", CS_UTF32LE}, {"UCS-4LE", CS_UTF32LE}, {"WCHAR_T", CS_UTF32LE},
    {"UTF-32BE", CS_UTF32BE}, {"UCS-4BE", CS_UTF32BE},
};

typedef struct {
    Charset from;
    Charset to;
    int is_ignoring;
    int is_transliterating;
} Converter;

/* Decoding outcomes besides a code point. */
enum { DECODED = 0, INCOMPLETE = 1, INVALID = 2 };

static int parse_name(const char *text, Charset *out, int *is_ignoring, int *is_transliterating)
{
    char name[NAME_MAX_LENGTH];
    size_t length = 0;
    while (text[length] != '\0' && text[length] != '/' && length < sizeof(name) - 1) {
        name[length] = (char)toupper((unsigned char)text[length]);
        length++;
    }
    name[length] = '\0';
    const char *options = text + length;
    *is_ignoring |= strstr(options, "IGNORE") != NULL;
    *is_transliterating |= strstr(options, "TRANSLIT") != NULL;
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        if (strcmp(name, NAMES[i].name) == 0) {
            *out = NAMES[i].charset;
            return 1;
        }
    }
    return 0;
}

LIBC_FN void *sysv_iconv_open(const char *to, const char *from)
{
    Converter *converter = (Converter *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Converter));
    if (converter == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return ICONV_ERROR;
    }
    if (!parse_name(to, &converter->to, &converter->is_ignoring, &converter->is_transliterating) ||
        !parse_name(from, &converter->from, &converter->is_ignoring, &converter->is_transliterating)) {
        HeapFree(GetProcessHeap(), 0, converter);
        shim_set_errno(LINUX_EINVAL);
        return ICONV_ERROR;
    }
    return converter;
}

LIBC_FN int sysv_iconv_close(void *handle)
{
    HeapFree(GetProcessHeap(), 0, handle);
    return 0;
}

static uint32_t read_unit(const uint8_t *at, size_t width, int is_big_endian)
{
    uint32_t value = 0;
    for (size_t i = 0; i < width; i++) {
        size_t index = is_big_endian ? i : width - 1 - i;
        value = (value << 8) | at[index];
    }
    return value;
}

static int decode_utf8(const uint8_t *at, size_t available, uint32_t *code, size_t *used)
{
    unsigned length = at[0] < 0x80 ? 1 : (at[0] >= 0xc2 && at[0] <= 0xdf ? 2 : (at[0] >= 0xe0 && at[0] <= 0xef ? 3 : (at[0] >= 0xf0 && at[0] <= 0xf4 ? 4 : 0)));
    if (length == 0) {
        return INVALID;
    }
    if (available < length) {
        return INCOMPLETE;
    }
    uint32_t value = length == 1 ? at[0] : (at[0] & (0xffu >> (length + 1)));
    for (unsigned i = 1; i < length; i++) {
        if ((at[i] & 0xc0u) != 0x80u) {
            return INVALID;
        }
        value = (value << 6) | (at[i] & 0x3fu);
    }
    *code = value;
    *used = length;
    return (value > MAX_CODE_POINT || (value >= SURROGATE_FIRST && value <= SURROGATE_LAST)) ? INVALID : DECODED;
}

static int decode_utf16(const uint8_t *at, size_t available, int is_big_endian, uint32_t *code, size_t *used)
{
    if (available < 2) {
        return INCOMPLETE;
    }
    uint32_t first = read_unit(at, 2, is_big_endian);
    *used = 2;
    *code = first;
    if (first < SURROGATE_FIRST || first > SURROGATE_LAST) {
        return DECODED;
    }
    if (first > 0xdbffu) {
        return INVALID;
    }
    if (available < 4) {
        return INCOMPLETE;
    }
    uint32_t second = read_unit(at + 2, 2, is_big_endian);
    if (second < 0xdc00u || second > SURROGATE_LAST) {
        return INVALID;
    }
    *code = 0x10000u + ((first - SURROGATE_FIRST) << 10) + (second - 0xdc00u);
    *used = 4;
    return DECODED;
}

static int decode(Charset charset, const uint8_t *at, size_t available, uint32_t *code, size_t *used)
{
    switch (charset) {
    case CS_ASCII:
        *code = at[0];
        *used = 1;
        return at[0] < 0x80 ? DECODED : INVALID;
    case CS_LATIN1:
        *code = at[0];
        *used = 1;
        return DECODED;
    case CS_UTF8:
        return decode_utf8(at, available, code, used);
    case CS_UTF16LE:
    case CS_UTF16BE:
        return decode_utf16(at, available, charset == CS_UTF16BE, code, used);
    default:
        if (available < 4) {
            return INCOMPLETE;
        }
        *code = read_unit(at, 4, charset == CS_UTF32BE);
        *used = 4;
        return *code > MAX_CODE_POINT ? INVALID : DECODED;
    }
}

static void write_unit(uint8_t *out, uint32_t value, size_t width, int is_big_endian)
{
    for (size_t i = 0; i < width; i++) {
        size_t index = is_big_endian ? width - 1 - i : i;
        out[index] = (uint8_t)(value >> (8 * i));
    }
}

static size_t encode_utf8(uint32_t code, uint8_t *out)
{
    if (code < 0x80) {
        out[0] = (uint8_t)code;
        return 1;
    }
    size_t length = code < 0x800 ? 2 : (code < 0x10000 ? 3 : 4);
    for (size_t i = length - 1; i > 0; i--) {
        out[i] = (uint8_t)(0x80u | (code & 0x3fu));
        code >>= 6;
    }
    out[0] = (uint8_t)((0xf00u >> length) | code);
    return length;
}

/* Writes one code point; returns the bytes written, or 0 when the target charset cannot hold it. */
static size_t encode(Charset charset, uint32_t code, uint8_t *out)
{
    switch (charset) {
    case CS_ASCII:
    case CS_LATIN1:
        out[0] = (uint8_t)code;
        return code < (charset == CS_ASCII ? 0x80u : 0x100u) ? 1 : 0;
    case CS_UTF8:
        return encode_utf8(code, out);
    case CS_UTF16LE:
    case CS_UTF16BE:
        if (code < 0x10000u) {
            write_unit(out, code, 2, charset == CS_UTF16BE);
            return 2;
        }
        write_unit(out, SURROGATE_FIRST + ((code - 0x10000u) >> 10), 2, charset == CS_UTF16BE);
        write_unit(out + 2, 0xdc00u + ((code - 0x10000u) & 0x3ffu), 2, charset == CS_UTF16BE);
        return 4;
    default:
        write_unit(out, code, 4, charset == CS_UTF32BE);
        return 4;
    }
}

LIBC_FN size_t sysv_iconv(void *handle, char **in, size_t *in_left, char **out, size_t *out_left)
{
    const Converter *converter = (const Converter *)handle;
    if (in == NULL || *in == NULL) {
        return 0; /* no shift states to reset */
    }
    while (*in_left > 0) {
        uint32_t code = 0;
        size_t used = 0;
        uint8_t encoded[4];
        int status = decode(converter->from, (const uint8_t *)*in, *in_left, &code, &used);
        if (status == INCOMPLETE) {
            return (size_t)shim_fail(LINUX_EINVAL);
        }
        if (status == INVALID && converter->is_ignoring) {
            *in += 1;
            *in_left -= 1;
            continue;
        }
        if (status == INVALID) {
            return (size_t)shim_fail(LINUX_EILSEQ);
        }
        size_t length = encode(converter->to, code, encoded);
        if (length == 0 && converter->is_transliterating) {
            encoded[0] = REPLACEMENT_BYTE;
            length = 1;
        }
        if (length == 0 && converter->is_ignoring) {
            *in += used;
            *in_left -= used;
            continue;
        }
        if (length == 0) {
            return (size_t)shim_fail(LINUX_EILSEQ);
        }
        if (*out_left < length) {
            return (size_t)shim_fail(LINUX_E2BIG);
        }
        memcpy(*out, encoded, length);
        *out += length;
        *out_left -= length;
        *in += used;
        *in_left -= used;
    }
    return 0;
}
