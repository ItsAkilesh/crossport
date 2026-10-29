/*
 * File: shim/sysmisc.c
 * Purpose: syscall(), processor counts, floating-point rounding mode, long double scaling and wide-character stdio
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <float.h>
#include <math.h>
#include <string.h>

#include "libc_abi.h"

/* Linux x86-64 system call numbers the shim answers. */
enum {
    SYS_READ = 0, SYS_WRITE = 1, SYS_SCHED_YIELD = 24, SYS_GETPID = 39, SYS_GETTID = 186, SYS_CLOCK_GETTIME = 228,
    SYS_GETRANDOM = 318
};

/* x87 control-word rounding bits (what FE_TONEAREST and friends are on x86-64). */
enum { FE_TONEAREST = 0, FE_DOWNWARD = 0x400, FE_UPWARD = 0x800, FE_TOWARDZERO = 0xc00 };

#define WEOF_VALUE 0xffffffffu
#define MAX_UTF8_BYTES 4

typedef int32_t WChar;

LIBC_FN int sysv_getpid(void);
LIBC_FN int64_t sysv_read(int fd, void *buffer, size_t count);
LIBC_FN int64_t sysv_write(int fd, const void *buffer, size_t count);
LIBC_FN int sysv_clock_gettime(int clock, void *spec);
LIBC_FN int64_t sysv_getrandom(void *buffer, size_t count, unsigned flags);
LIBC_FN size_t sysv_wcrtomb(char *out, WChar wide, void *state);
LIBC_FN size_t sysv_mbrtowc(WChar *out, const char *text, size_t count, void *state);
LIBC_FN int sysv_fgetc(void *stream);
LIBC_FN int sysv_fputc(int c, void *stream);
LIBC_FN int sysv_ungetc(int c, void *stream);
LIBC_FN size_t sysv_fwrite(const void *buffer, size_t size, size_t count, void *stream);
extern void *crossport_data_stdin;
extern void *crossport_data_stdout;

LIBC_FN int64_t sysv_syscall(int64_t number, int64_t first, int64_t second, int64_t third, int64_t fourth, int64_t fifth)
{
    (void)fourth;
    (void)fifth;
    switch (number) {
    case SYS_READ:
        return sysv_read((int)first, (void *)second, (size_t)third);
    case SYS_WRITE:
        return sysv_write((int)first, (const void *)second, (size_t)third);
    case SYS_SCHED_YIELD:
        SwitchToThread();
        return 0;
    case SYS_GETPID:
        return sysv_getpid();
    case SYS_GETTID:
        return (int64_t)GetCurrentThreadId();
    case SYS_CLOCK_GETTIME:
        return sysv_clock_gettime((int)first, (void *)second);
    case SYS_GETRANDOM:
        return sysv_getrandom((void *)first, (size_t)second, (unsigned)third);
    default:
        return shim_fail(LINUX_ENOSYS);
    }
}

LIBC_FN int sysv_get_nprocs(void)
{
    return (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
}

LIBC_FN int sysv_get_nprocs_conf(void)
{
    return (int)GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
}

LIBC_FN int sysv_sched_yield(void)
{
    SwitchToThread();
    return 0;
}

LIBC_FN int sysv_fegetround(void)
{
    switch (_controlfp(0, 0) & _MCW_RC) {
    case _RC_DOWN:
        return FE_DOWNWARD;
    case _RC_UP:
        return FE_UPWARD;
    case _RC_CHOP:
        return FE_TOWARDZERO;
    default:
        return FE_TONEAREST;
    }
}

/* Returns 0 on success, nonzero for a mode the processor does not have. */
LIBC_FN int sysv_fesetround(int mode)
{
    unsigned windows_mode;
    switch (mode) {
    case FE_TONEAREST:
        windows_mode = _RC_NEAR;
        break;
    case FE_DOWNWARD:
        windows_mode = _RC_DOWN;
        break;
    case FE_UPWARD:
        windows_mode = _RC_UP;
        break;
    case FE_TOWARDZERO:
        windows_mode = _RC_CHOP;
        break;
    default:
        return 1;
    }
    _controlfp(windows_mode, _MCW_RC);
    return 0;
}

LIBC_FN long double sysv_frexpl(long double value, int *exponent)
{
    return frexpl(value, exponent);
}

LIBC_FN long double sysv_ldexpl(long double value, int exponent)
{
    return ldexpl(value, exponent);
}

/* Wide stdio writes and reads UTF-8 through the byte functions; only whole characters are exchanged. */
LIBC_FN uint32_t sysv_fputwc(WChar wide, void *stream)
{
    char bytes[MAX_UTF8_BYTES];
    size_t length = sysv_wcrtomb(bytes, wide, NULL);
    if (length == (size_t)-1 || sysv_fwrite(bytes, 1, length, stream) != length) {
        return WEOF_VALUE;
    }
    return (uint32_t)wide;
}

LIBC_FN uint32_t sysv_putwc(WChar wide, void *stream)
{
    return sysv_fputwc(wide, stream);
}

LIBC_FN uint32_t sysv_putwchar(WChar wide)
{
    return sysv_fputwc(wide, crossport_data_stdout);
}

LIBC_FN uint32_t sysv_fgetwc(void *stream)
{
    char bytes[MAX_UTF8_BYTES];
    int first = sysv_fgetc(stream);
    if (first < 0) {
        return WEOF_VALUE;
    }
    bytes[0] = (char)first;
    for (size_t length = 1; length <= MAX_UTF8_BYTES; length++) {
        WChar wide;
        size_t used = sysv_mbrtowc(&wide, bytes, length, NULL);
        if (used != (size_t)-2) {
            return used == (size_t)-1 ? WEOF_VALUE : (uint32_t)wide;
        }
        int next = sysv_fgetc(stream);
        if (next < 0) {
            return WEOF_VALUE;
        }
        bytes[length] = (char)next;
    }
    return WEOF_VALUE;
}

LIBC_FN uint32_t sysv_getwc(void *stream)
{
    return sysv_fgetwc(stream);
}

LIBC_FN uint32_t sysv_getwchar(void)
{
    return sysv_fgetwc(crossport_data_stdin);
}

/* Only a character that is one byte in UTF-8 can be pushed back: the byte layer holds a single byte. */
LIBC_FN uint32_t sysv_ungetwc(uint32_t wide, void *stream)
{
    if (wide >= 0x80 || wide == WEOF_VALUE) {
        return WEOF_VALUE;
    }
    return sysv_ungetc((int)wide, stream) < 0 ? WEOF_VALUE : wide;
}
