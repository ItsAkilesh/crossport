/*
 * File: shim/wprintf.c
 * Purpose: The wide-character printf family (swprintf, vswprintf, wprintf, vwprintf, fwprintf, vfwprintf) and the fortified forms
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the third application sweep (cal and ncal import __swprintf_chk and __wprintf_chk). The format is converted to UTF-8, formatted by the
 * narrow printf (whose %s, %ls, %c and %lc mean the same thing in both families), and the result converted back or written to the stream, which
 * is bytes in a UTF-8 locale. A field width counts bytes rather than characters for non-ASCII text.
 */

#include <string.h>

#include "libc_abi.h"
#include "stdio_internal.h"

typedef int32_t Wide;

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN size_t sysv_wcstombs(char *out, const Wide *source, size_t limit);
LIBC_FN size_t sysv_mbstowcs(Wide *out, const char *source, size_t limit);
LIBC_FN int sysv_vasprintf(char **out, const char *format, __builtin_sysv_va_list ap);
LIBC_FN int sysv_fputs(const char *text, ShimFile *file);
LIBC_FN NORETURN void sysv___chk_fail(void);

/* Formats into a malloc'd UTF-8 string. Returns its length in bytes, or -1. */
static int format_to_utf8(const Wide *format, __builtin_sysv_va_list arguments, char **text)
{
    size_t needed = sysv_wcstombs(NULL, format, 0);
    if (needed == (size_t)-1) {
        return shim_fail(LINUX_EILSEQ);
    }
    char *narrow = sysv_malloc(needed + 1);
    if (narrow == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    sysv_wcstombs(narrow, format, needed + 1);
    int length = sysv_vasprintf(text, narrow, arguments);
    sysv_free(narrow);
    return length;
}

/* The number of characters in a UTF-8 string (bytes that are not continuation bytes). */
static int character_count(const char *text)
{
    int count = 0;
    for (; *text != '\0'; text++) {
        count += ((unsigned char)*text & 0xC0) != 0x80;
    }
    return count;
}

LIBC_FN int sysv_vswprintf(Wide *buffer, size_t size, const Wide *format, __builtin_sysv_va_list arguments)
{
    char *text;
    if (format_to_utf8(format, arguments, &text) < 0) {
        return -1;
    }
    int characters = character_count(text);
    int fits = size > 0 && (size_t)characters < size;
    if (size > 0) {
        Wide *wide = sysv_malloc(((size_t)characters + 1) * sizeof(Wide));
        if (wide != NULL) {
            sysv_mbstowcs(wide, text, (size_t)characters + 1);
            size_t copied = fits ? (size_t)characters : size - 1;
            memcpy(buffer, wide, copied * sizeof(Wide));
            buffer[copied] = 0;
            sysv_free(wide);
        }
    }
    sysv_free(text);
    return fits ? characters : -1;
}

LIBC_FN int sysv_swprintf(Wide *buffer, size_t size, const Wide *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    int result = sysv_vswprintf(buffer, size, format, arguments);
    __builtin_sysv_va_end(arguments);
    return result;
}

LIBC_FN int sysv_vfwprintf(ShimFile *stream, const Wide *format, __builtin_sysv_va_list arguments)
{
    char *text;
    if (format_to_utf8(format, arguments, &text) < 0) {
        return -1;
    }
    int characters = character_count(text);
    int failed = sysv_fputs(text, stream) < 0;
    sysv_free(text);
    return failed ? -1 : characters;
}

LIBC_FN int sysv_vwprintf(const Wide *format, __builtin_sysv_va_list arguments)
{
    return sysv_vfwprintf(crossport_data_stdout, format, arguments);
}

LIBC_FN int sysv_fwprintf(ShimFile *stream, const Wide *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    int result = sysv_vfwprintf(stream, format, arguments);
    __builtin_sysv_va_end(arguments);
    return result;
}

LIBC_FN int sysv_wprintf(const Wide *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    int result = sysv_vfwprintf(crossport_data_stdout, format, arguments);
    __builtin_sysv_va_end(arguments);
    return result;
}

LIBC_FN int sysv___swprintf_chk(Wide *buffer, size_t size, int flag, size_t buffer_size, const Wide *format, ...)
{
    (void)flag;
    if (size > buffer_size) {
        sysv___chk_fail();
    }
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    int result = sysv_vswprintf(buffer, size, format, arguments);
    __builtin_sysv_va_end(arguments);
    return result;
}

LIBC_FN int sysv___vswprintf_chk(Wide *buffer, size_t size, int flag, size_t buffer_size, const Wide *format, __builtin_sysv_va_list arguments)
{
    (void)flag;
    if (size > buffer_size) {
        sysv___chk_fail();
    }
    return sysv_vswprintf(buffer, size, format, arguments);
}

LIBC_FN int sysv___wprintf_chk(int flag, const Wide *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    int result = sysv_vfwprintf(crossport_data_stdout, format, arguments);
    __builtin_sysv_va_end(arguments);
    return result;
}

LIBC_FN int sysv___fwprintf_chk(ShimFile *stream, int flag, const Wide *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    int result = sysv_vfwprintf(stream, format, arguments);
    __builtin_sysv_va_end(arguments);
    return result;
}
