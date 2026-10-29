/*
 * File: shim/time_more.c
 * Purpose: Terminal attribute stubs, wcwidth, clock_getres, times, asctime and ctime
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define TICKS_PER_SECOND 100
#define ASCTIME_BYTES 26

LIBC_FN void *sysv_gmtime_r(const int64_t *timer, void *tm);

/* Terminal attributes: a console is not a termios device, so every query says "not a terminal". */
LIBC_FN int sysv_tcgetattr(int fd, void *attributes)
{
    (void)fd;
    (void)attributes;
    return shim_fail(LINUX_ENOTTY);
}

LIBC_FN int sysv_tcsetattr(int fd, int action, const void *attributes)
{
    (void)fd;
    (void)action;
    (void)attributes;
    return shim_fail(LINUX_ENOTTY);
}

LIBC_FN int sysv_mbsinit(const void *state)
{
    return state == NULL || *(const uint64_t *)state == 0;
}

LIBC_FN char *sysv_strerror_l(int number, void *locale)
{
    (void)locale;
    extern LIBC_FN char *sysv_strerror(int);
    return sysv_strerror(number);
}

LIBC_FN char *sysv_dcngettext(const char *domain, const char *singular, const char *plural, unsigned long count, int category)
{
    (void)domain;
    (void)category;
    return (char *)(count == 1 ? singular : plural);
}

LIBC_FN int64_t sysv_wcwidth(uint32_t c)
{
    if (c == 0) {
        return 0;
    }
    if (c < 32 || (c >= 0x7f && c < 0xa0)) {
        return -1;
    }
    if (c >= 0x300 && c <= 0x36f) {
        return 0; /* combining marks */
    }
    int is_wide = (c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) || (c >= 0xac00 && c <= 0xd7a3) ||
                  (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe6f) || (c >= 0xff00 && c <= 0xff60) ||
                  (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x1f300 && c <= 0x1f64f) || (c >= 0x20000 && c <= 0x3fffd);
    return is_wide ? 2 : 1;
}

LIBC_FN int64_t sysv_clock_getres(int clock, int64_t *resolution)
{
    (void)clock;
    if (resolution != NULL) {
        resolution[0] = 0;
        resolution[1] = 1; /* 1 ns: the precise system time functions are used */
    }
    return 0;
}

/* struct tms: process and child times in clock ticks. */
LIBC_FN int64_t sysv_times(int64_t *buffer)
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (buffer != NULL && GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        uint64_t user_units = ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
        uint64_t kernel_units = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        buffer[0] = (int64_t)(user_units / (10000000ull / TICKS_PER_SECOND));
        buffer[1] = (int64_t)(kernel_units / (10000000ull / TICKS_PER_SECOND));
        buffer[2] = buffer[3] = 0;
    }
    return (int64_t)(GetTickCount64() / (1000 / TICKS_PER_SECOND));
}

/* asctime and ctime: "Thu Jan  1 00:00:00 1970\n". `tm` is Linux's struct tm (56 bytes). */
LIBC_FN char *sysv_asctime_r(const int32_t *tm, char *buffer)
{
    static const char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int day = tm[6] >= 0 && tm[6] < 7 ? tm[6] : 0;
    int month = tm[4] >= 0 && tm[4] < 12 ? tm[4] : 0;
    char text[ASCTIME_BYTES + 8];
    int length = 0;
    memcpy(text, days[day], 3);
    text[3] = ' ';
    memcpy(text + 4, months[month], 3);
    length = 7;
    const int fields[] = {tm[3], tm[2], tm[1], tm[0]};
    (void)fields;
    char two[3];
    text[length++] = ' ';
    text[length++] = tm[3] < 10 ? ' ' : (char)('0' + tm[3] / 10);
    text[length++] = (char)('0' + tm[3] % 10);
    text[length++] = ' ';
    const int clock_fields[3] = {tm[2], tm[1], tm[0]};
    for (int i = 0; i < 3; i++) {
        two[0] = (char)('0' + clock_fields[i] / 10 % 10);
        two[1] = (char)('0' + clock_fields[i] % 10);
        text[length++] = two[0];
        text[length++] = two[1];
        text[length++] = i < 2 ? ':' : ' ';
    }
    int year = tm[5] + 1900;
    for (int divisor = 1000; divisor >= 1; divisor /= 10) {
        text[length++] = (char)('0' + year / divisor % 10);
    }
    text[length++] = '\n';
    text[length] = '\0';
    memcpy(buffer, text, (size_t)length + 1);
    return buffer;
}

LIBC_FN char *sysv_asctime(const int32_t *tm)
{
    static char buffer[ASCTIME_BYTES + 2];
    return sysv_asctime_r(tm, buffer);
}

LIBC_FN char *sysv_ctime_r(const int64_t *timer, char *buffer)
{
    int32_t tm[14];
    sysv_gmtime_r(timer, tm);
    return sysv_asctime_r(tm, buffer);
}

LIBC_FN char *sysv_ctime(const int64_t *timer)
{
    static char buffer[ASCTIME_BYTES + 2];
    return sysv_ctime_r(timer, buffer);
}
