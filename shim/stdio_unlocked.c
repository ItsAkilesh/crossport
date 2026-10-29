/*
 * File: shim/stdio_unlocked.c
 * Purpose: The _unlocked stdio names, which are the locked functions here
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "stdio_internal.h"

LIBC_FN int sysv_fgetc(ShimFile *file);
LIBC_FN int sysv_fputc(int c, ShimFile *file);
LIBC_FN char *sysv_fgets(char *buffer, int size, ShimFile *file);
LIBC_FN size_t sysv_fwrite(const void *buffer, size_t size, size_t count, ShimFile *file);
LIBC_FN int sysv_fputs(const char *text, ShimFile *file);
LIBC_FN int sysv_getc(ShimFile *file);
LIBC_FN int sysv_putc(int c, ShimFile *file);
LIBC_FN int sysv_fflush(ShimFile *file);
LIBC_FN int sysv_feof(ShimFile *file);
LIBC_FN int sysv_ferror(ShimFile *file);
LIBC_FN void sysv_clearerr(ShimFile *file);
LIBC_FN size_t sysv_fread(void *destination, size_t size, size_t count, ShimFile *file);
LIBC_FN char *sysv___fgets_chk(char *buffer, size_t capacity, int size, ShimFile *file);

/* The unlocked variants are the same functions: the shim's stream lock is cheap and recursive. */
LIBC_FN int sysv_getc_unlocked(ShimFile *file)
{
    return sysv_getc(file);
}

LIBC_FN int sysv_fgetc_unlocked(ShimFile *file)
{
    return sysv_fgetc(file);
}

LIBC_FN int sysv_getchar_unlocked(void)
{
    return sysv_fgetc(crossport_data_stdin);
}

LIBC_FN int sysv_putc_unlocked(int c, ShimFile *file)
{
    return sysv_putc(c, file);
}

LIBC_FN int sysv_fputc_unlocked(int c, ShimFile *file)
{
    return sysv_fputc(c, file);
}

LIBC_FN int sysv_putchar_unlocked(int c)
{
    return sysv_fputc(c, crossport_data_stdout);
}

LIBC_FN int sysv_fputs_unlocked(const char *text, ShimFile *file)
{
    return sysv_fputs(text, file);
}

LIBC_FN size_t sysv_fwrite_unlocked(const void *buffer, size_t size, size_t count, ShimFile *file)
{
    return sysv_fwrite(buffer, size, count, file);
}

LIBC_FN size_t sysv_fread_unlocked(void *destination, size_t size, size_t count, ShimFile *file)
{
    return sysv_fread(destination, size, count, file);
}

LIBC_FN int sysv_fflush_unlocked(ShimFile *file)
{
    return sysv_fflush(file);
}

LIBC_FN char *sysv_fgets_unlocked(char *buffer, int size, ShimFile *file)
{
    return sysv_fgets(buffer, size, file);
}

LIBC_FN char *sysv___fgets_unlocked_chk(char *buffer, size_t capacity, int size, ShimFile *file)
{
    return sysv___fgets_chk(buffer, capacity, size, file);
}

LIBC_FN int sysv_feof_unlocked(ShimFile *file)
{
    return sysv_feof(file);
}

LIBC_FN int sysv_ferror_unlocked(ShimFile *file)
{
    return sysv_ferror(file);
}

LIBC_FN void sysv_clearerr_unlocked(ShimFile *file)
{
    sysv_clearerr(file);
}
