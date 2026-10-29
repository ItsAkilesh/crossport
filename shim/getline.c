/*
 * File: shim/getline.c
 * Purpose: getline, getdelim, asprintf and dprintf
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "stdio_internal.h"

#define LINE_START_CAPACITY 120

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN int sysv_fgetc(ShimFile *file);
LIBC_FN int sysv_vsnprintf(char *buffer, size_t size, const char *format, __builtin_sysv_va_list ap);
LIBC_FN int64_t sysv_write(int fd, const void *buffer, size_t count);

/* Reads up to and including `delimiter` into a growing heap buffer. Returns the length read, or -1 at end of input. */
LIBC_FN int64_t sysv_getdelim(char **line, size_t *capacity, int delimiter, ShimFile *file)
{
    if (line == NULL || capacity == NULL) {
        return shim_fail(LINUX_EINVAL);
    }
    if (*line == NULL || *capacity == 0) {
        *capacity = LINE_START_CAPACITY;
        *line = (char *)sysv_malloc(*capacity);
        if (*line == NULL) {
            return shim_fail(LINUX_ENOMEM);
        }
    }
    size_t length = 0;
    int c;
    while ((c = sysv_fgetc(file)) != SHIM_EOF) {
        if (length + 2 > *capacity) {
            char *grown = (char *)sysv_realloc(*line, *capacity * 2);
            if (grown == NULL) {
                return shim_fail(LINUX_ENOMEM);
            }
            *line = grown;
            *capacity *= 2;
        }
        (*line)[length++] = (char)c;
        if (c == delimiter) {
            break;
        }
    }
    if (length == 0) {
        return -1;
    }
    (*line)[length] = '\0';
    return (int64_t)length;
}

LIBC_FN int64_t sysv___getdelim(char **line, size_t *capacity, int delimiter, ShimFile *file)
{
    return sysv_getdelim(line, capacity, delimiter, file);
}

LIBC_FN int64_t sysv_getline(char **line, size_t *capacity, ShimFile *file)
{
    return sysv_getdelim(line, capacity, '\n', file);
}

LIBC_FN int sysv_getchar(void)
{
    return sysv_fgetc(crossport_data_stdin);
}

LIBC_FN int sysv_vasprintf(char **out, const char *format, __builtin_sysv_va_list ap)
{
    __builtin_sysv_va_list copy;
    __builtin_sysv_va_copy(copy, ap);
    int length = sysv_vsnprintf(NULL, 0, format, copy);
    __builtin_sysv_va_end(copy);
    if (length < 0) {
        return -1;
    }
    *out = (char *)sysv_malloc((size_t)length + 1);
    if (*out == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    return sysv_vsnprintf(*out, (size_t)length + 1, format, ap);
}

LIBC_FN int sysv_asprintf(char **out, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vasprintf(out, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___asprintf_chk(char **out, int flag, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vasprintf(out, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___vasprintf_chk(char **out, int flag, const char *format, __builtin_sysv_va_list ap)
{
    (void)flag;
    return sysv_vasprintf(out, format, ap);
}

LIBC_FN int sysv_vdprintf(int fd, const char *format, __builtin_sysv_va_list ap)
{
    char *text = NULL;
    int length = sysv_vasprintf(&text, format, ap);
    if (length >= 0 && sysv_write(fd, text, (size_t)length) != length) {
        length = -1;
    }
    sysv_free(text);
    return length;
}

LIBC_FN int sysv_dprintf(int fd, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vdprintf(fd, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___dprintf_chk(int fd, int flag, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vdprintf(fd, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}
