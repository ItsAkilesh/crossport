/*
 * File: shim/err_family.c
 * Purpose: The BSD error-printing calls (err, errx, warn, warnx and their v-forms) and error_at_line
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the compatibility survey: 65 programs of Ubuntu's /usr/bin import warnx and 56 import errx. The messages follow glibc:
 * "name: text: reason" for warn and err, "name: text" for warnx and errx, with the short program name (__progname).
 */

#include <string.h>

#include "libc_abi.h"
#include "startinfo.h"
#include "stdio_internal.h"

LIBC_FN int sysv_fflush(ShimFile *file);
LIBC_FN int sysv_vfprintf(ShimFile *file, const char *format, __builtin_sysv_va_list ap);
LIBC_FN int sysv_fprintf(ShimFile *file, const char *format, ...);
LIBC_FN int sysv_fputs(const char *text, ShimFile *file);
LIBC_FN int sysv_fputc(int byte, ShimFile *file);
LIBC_FN char *sysv_strerror(int value);
LIBC_FN NORETURN void sysv_exit(int status);

extern char *crossport_data___progname;
extern char *crossport_data_program_invocation_name;

/* Writes "name: " then the formatted text (when there is any) and, for the errno forms, the reason, then a newline. */
static void print_message(const char *format, __builtin_sysv_va_list arguments, int has_reason, int reason)
{
    ShimFile *stream = crossport_data_stderr;
    const char *name = *(char **)shim_data_home(&crossport_data___progname);
    sysv_fflush(crossport_data_stdout);
    sysv_fputs(name != NULL ? name : "", stream);
    sysv_fputs(": ", stream);
    if (format != NULL) {
        sysv_vfprintf(stream, format, arguments);
        if (has_reason) {
            sysv_fputs(": ", stream);
        }
    }
    if (has_reason) {
        sysv_fputs(sysv_strerror(reason), stream);
    }
    sysv_fputc('\n', stream);
    sysv_fflush(stream);
}

LIBC_FN void sysv_vwarn(const char *format, __builtin_sysv_va_list arguments)
{
    int saved = *shim_errno_ptr();
    print_message(format, arguments, 1, saved);
    *shim_errno_ptr() = saved;
}

LIBC_FN void sysv_vwarnx(const char *format, __builtin_sysv_va_list arguments)
{
    print_message(format, arguments, 0, 0);
}

LIBC_FN NORETURN void sysv_verr(int status, const char *format, __builtin_sysv_va_list arguments)
{
    print_message(format, arguments, 1, *shim_errno_ptr());
    sysv_exit(status);
}

LIBC_FN NORETURN void sysv_verrx(int status, const char *format, __builtin_sysv_va_list arguments)
{
    print_message(format, arguments, 0, 0);
    sysv_exit(status);
}

LIBC_FN void sysv_warn(const char *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    sysv_vwarn(format, arguments);
    __builtin_sysv_va_end(arguments);
}

LIBC_FN void sysv_warnx(const char *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    sysv_vwarnx(format, arguments);
    __builtin_sysv_va_end(arguments);
}

LIBC_FN NORETURN void sysv_err(int status, const char *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    sysv_verr(status, format, arguments);
}

LIBC_FN NORETURN void sysv_errx(int status, const char *format, ...)
{
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    sysv_verrx(status, format, arguments);
}

/* error_at_line(3): "invocation-name:file:line: text[: reason]" (the full name, as error does); exits when status is non-zero. */
LIBC_FN void sysv_error_at_line(int status, int errnum, const char *file, unsigned line, const char *format, ...)
{
    ShimFile *stream = crossport_data_stderr;
    const char *name = *(char **)shim_data_home(&crossport_data_program_invocation_name);
    sysv_fflush(crossport_data_stdout);
    sysv_fprintf(stream, "%s:%s:%u: ", name != NULL ? name : "", file, line);
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    sysv_vfprintf(stream, format, arguments);
    __builtin_sysv_va_end(arguments);
    if (errnum != 0) {
        sysv_fputs(": ", stream);
        sysv_fputs(sysv_strerror(errnum), stream);
    }
    sysv_fputc('\n', stream);
    sysv_fflush(stream);
    if (status != 0) {
        sysv_exit(status);
    }
}
