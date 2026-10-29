/*
 * File: shim/scanf_stream.c
 * Purpose: scanf and fscanf, which scan one line of a stream at a time
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "stdio_internal.h"

#define MAX_SCAN_LINE 4096

LIBC_FN char *sysv_fgets(char *buffer, int size, ShimFile *file);
LIBC_FN int sysv_vsscanf(const char *text, const char *format, __builtin_sysv_va_list ap);

/*
 * Scanning from a stream works on one line at a time: a line is read, scanned, and what the format did
 * not consume of it is dropped. That is the common use (one conversion per line); a program that scans
 * part of a line and then reads the rest with another call sees the rest missing.
 */
LIBC_FN int sysv_vfscanf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    char line[MAX_SCAN_LINE];
    if (sysv_fgets(line, sizeof(line), file) == NULL) {
        return -1;
    }
    return sysv_vsscanf(line, format, ap);
}

LIBC_FN int sysv_fscanf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv_vscanf(const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(crossport_data_stdin, format, ap);
}

LIBC_FN int sysv_scanf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(crossport_data_stdin, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_fscanf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc23_fscanf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_scanf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(crossport_data_stdin, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc23_scanf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = sysv_vfscanf(crossport_data_stdin, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_vfscanf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(file, format, ap);
}

LIBC_FN int sysv___isoc23_vfscanf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(file, format, ap);
}

LIBC_FN int sysv___isoc99_vscanf(const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(crossport_data_stdin, format, ap);
}

LIBC_FN int sysv___isoc23_vscanf(const char *format, __builtin_sysv_va_list ap)
{
    return sysv_vfscanf(crossport_data_stdin, format, ap);
}
