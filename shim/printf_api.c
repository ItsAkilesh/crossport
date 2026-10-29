/*
 * File: shim/printf_api.c
 * Purpose: The public printf family: printf, fprintf, snprintf, sprintf, their v forms and glibc's fortify (_chk) variants
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "printf_internal.h"

#define CHK_FAILURE_EXIT_CODE 134

static SYSV int to_stream(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    Out out = {.file = file};
    file_lock(file);
    int result = printf_core(&out, format, ap);
    file_unlock(file);
    if (result < 0) {
        file->flags |= IO_ERR_SEEN;
    }
    return result;
}

static SYSV int to_string(char *buffer, size_t size, const char *format, __builtin_sysv_va_list ap)
{
    Out out = {.buffer = buffer, .capacity = size};
    int result = printf_core(&out, format, ap);
    if (size > 0) {
        buffer[out.written < size ? out.written : size - 1] = '\0';
    }
    return result;
}

LIBC_FN int sysv_vfprintf(ShimFile *file, const char *format, __builtin_sysv_va_list ap)
{
    return to_stream(file, format, ap);
}

LIBC_FN int sysv_vprintf(const char *format, __builtin_sysv_va_list ap)
{
    return to_stream(crossport_data_stdout, format, ap);
}

LIBC_FN int sysv_vsnprintf(char *buffer, size_t size, const char *format, __builtin_sysv_va_list ap)
{
    return to_string(buffer, size, format, ap);
}

LIBC_FN int sysv_vsprintf(char *buffer, const char *format, __builtin_sysv_va_list ap)
{
    return to_string(buffer, SIZE_MAX / 2, format, ap);
}

LIBC_FN int sysv_printf(const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = to_stream(crossport_data_stdout, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv_fprintf(ShimFile *file, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = to_stream(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv_snprintf(char *buffer, size_t size, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = to_string(buffer, size, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv_sprintf(char *buffer, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = to_string(buffer, SIZE_MAX / 2, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

/* The fortify variants take a `flag` argument and, for strings, the destination's real size. */
NORETURN static void overflow_detected(void)
{
    static const char message[] = "*** buffer overflow detected ***: terminated\n";
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1);
    ExitProcess(CHK_FAILURE_EXIT_CODE);
    __builtin_unreachable();
}

static SYSV int checked_string(char *buffer, size_t size, size_t destination_size, const char *format, __builtin_sysv_va_list ap)
{
    if (size > destination_size) {
        overflow_detected();
    }
    return to_string(buffer, size, format, ap);
}

LIBC_FN int sysv___vfprintf_chk(ShimFile *file, int flag, const char *format, __builtin_sysv_va_list ap)
{
    (void)flag;
    return to_stream(file, format, ap);
}

LIBC_FN int sysv___vprintf_chk(int flag, const char *format, __builtin_sysv_va_list ap)
{
    (void)flag;
    return to_stream(crossport_data_stdout, format, ap);
}

LIBC_FN int sysv___vsnprintf_chk(char *buffer, size_t size, int flag, size_t destination_size, const char *format,
                                 __builtin_sysv_va_list ap)
{
    (void)flag;
    return checked_string(buffer, size, destination_size, format, ap);
}

LIBC_FN int sysv___vsprintf_chk(char *buffer, int flag, size_t destination_size, const char *format, __builtin_sysv_va_list ap)
{
    (void)flag;
    return checked_string(buffer, destination_size, destination_size, format, ap);
}

LIBC_FN int sysv___printf_chk(int flag, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = to_stream(crossport_data_stdout, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___fprintf_chk(ShimFile *file, int flag, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = to_stream(file, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___snprintf_chk(char *buffer, size_t size, int flag, size_t destination_size, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = checked_string(buffer, size, destination_size, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___sprintf_chk(char *buffer, int flag, size_t destination_size, const char *format, ...)
{
    (void)flag;
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = checked_string(buffer, destination_size, destination_size, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN size_t sysv___fread_chk(void *destination, size_t destination_size, size_t size, size_t count, ShimFile *file)
{
    if (size != 0 && count > destination_size / size) {
        overflow_detected();
    }
    return sysv_fread(destination, size, count, file);
}
