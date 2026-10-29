/*
 * File: shim/time.c
 * Purpose: Clocks, sleeping, and UTC calendar conversion (gmtime, timegm, mktime); local time is UTC
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Local time zones are not implemented: localtime and mktime behave as UTC, which is what a glibc
 * system with TZ=UTC or no zone data does. The corpus pins TZ=UTC.
 */

#include "time_internal.h"

#define SECONDS_PER_DAY 86400
#define UNIX_EPOCH_AS_FILETIME 116444736000000000ull
#define TICKS_PER_SECOND 10000000ull
#define NANOSECONDS_PER_TICK 100ull
#define NANOSECONDS_PER_SECOND 1000000000ll
#define CLOCK_REALTIME_ID 0
#define CLOCK_MONOTONIC_ID 1
#define CLOCK_PROCESS_CPU_ID 2
#define CLOCK_THREAD_CPU_ID 3
#define CLOCKS_PER_SECOND 1000000

static const char g_utc_zone[] = "UTC";

static uint64_t filetime_ticks(FILETIME t)
{
    return ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime;
}

static int64_t realtime_ns(void)
{
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    return (int64_t)((filetime_ticks(now) - UNIX_EPOCH_AS_FILETIME) * NANOSECONDS_PER_TICK);
}

LIBC_FN int64_t sysv_time(int64_t *out)
{
    int64_t seconds = realtime_ns() / NANOSECONDS_PER_SECOND;
    if (out != NULL) {
        *out = seconds;
    }
    return seconds;
}

LIBC_FN int sysv_gettimeofday(int64_t *tv, void *zone)
{
    (void)zone;
    int64_t ns = realtime_ns();
    tv[0] = ns / NANOSECONDS_PER_SECOND;
    tv[1] = (ns % NANOSECONDS_PER_SECOND) / 1000;
    return 0;
}

static int64_t cpu_ns(HANDLE process_or_thread, int is_thread)
{
    FILETIME created, exited, kernel, user;
    BOOL ok = is_thread ? GetThreadTimes(process_or_thread, &created, &exited, &kernel, &user)
                        : GetProcessTimes(process_or_thread, &created, &exited, &kernel, &user);
    return ok ? (int64_t)((filetime_ticks(kernel) + filetime_ticks(user)) * NANOSECONDS_PER_TICK) : 0;
}

LIBC_FN int sysv_clock_gettime(int clock_id, int64_t *ts)
{
    int64_t ns;
    LARGE_INTEGER counter, frequency;
    switch (clock_id) {
    case CLOCK_REALTIME_ID:
        ns = realtime_ns();
        break;
    case CLOCK_MONOTONIC_ID:
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);
        ns = (int64_t)((double)counter.QuadPart * 1e9 / (double)frequency.QuadPart);
        break;
    case CLOCK_PROCESS_CPU_ID:
        ns = cpu_ns(GetCurrentProcess(), 0);
        break;
    case CLOCK_THREAD_CPU_ID:
        ns = cpu_ns(GetCurrentThread(), 1);
        break;
    default:
        return shim_fail(LINUX_EINVAL);
    }
    ts[0] = ns / NANOSECONDS_PER_SECOND;
    ts[1] = ns % NANOSECONDS_PER_SECOND;
    return 0;
}

LIBC_FN c_long sysv_clock(void)
{
    return (c_long)(cpu_ns(GetCurrentProcess(), 0) / (NANOSECONDS_PER_SECOND / CLOCKS_PER_SECOND));
}

LIBC_FN double sysv_difftime(int64_t end, int64_t start)
{
    return (double)(end - start);
}

LIBC_FN int sysv_nanosleep(const int64_t *request, int64_t *remaining)
{
    if (request[0] < 0 || request[1] < 0 || request[1] >= NANOSECONDS_PER_SECOND) {
        return shim_fail(LINUX_EINVAL);
    }
    uint64_t milliseconds = (uint64_t)request[0] * 1000 + (uint64_t)(request[1] + 999999) / 1000000;
    Sleep(milliseconds > 0xFFFFFFFEu ? 0xFFFFFFFEu : (DWORD)milliseconds);
    if (remaining != NULL) {
        remaining[0] = remaining[1] = 0;
    }
    return 0;
}

LIBC_FN int sysv_usleep(unsigned microseconds)
{
    Sleep((microseconds + 999) / 1000);
    return 0;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm). */
int64_t time_days_from_civil(int64_t year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

static void civil_from_days(int64_t days, int64_t *year, unsigned *month, unsigned *day)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned day_of_era = (unsigned)(days - era * 146097);
    unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    unsigned shifted_month = (5 * day_of_year + 2) / 153;
    *day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    *month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    *year = year_of_era + era * 400 + (*month <= 2);
}

LIBC_FN Tm *sysv_gmtime_r(const int64_t *timer, Tm *out)
{
    int64_t seconds = *timer;
    int64_t days = seconds >= 0 ? seconds / SECONDS_PER_DAY : -((-seconds + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY);
    int64_t rest = seconds - days * SECONDS_PER_DAY;
    int64_t year;
    unsigned month, day;
    civil_from_days(days, &year, &month, &day);
    if (year - 1900 > INT32_MAX || year - 1900 < INT32_MIN) {
        shim_set_errno(LINUX_EOVERFLOW);
        return NULL;
    }
    out->tm_sec = (int)(rest % 60);
    out->tm_min = (int)(rest / 60 % 60);
    out->tm_hour = (int)(rest / 3600);
    out->tm_mday = (int)day;
    out->tm_mon = (int)month - 1;
    out->tm_year = (int)(year - 1900);
    out->tm_wday = (int)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
    out->tm_yday = (int)(days - time_days_from_civil(year, 1, 1));
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = g_utc_zone;
    return out;
}

LIBC_FN Tm *sysv_localtime_r(const int64_t *timer, Tm *out)
{
    return sysv_gmtime_r(timer, out);
}

LIBC_FN Tm *sysv_gmtime(const int64_t *timer)
{
    static Tm shared;
    return sysv_gmtime_r(timer, &shared);
}

LIBC_FN Tm *sysv_localtime(const int64_t *timer)
{
    return sysv_gmtime(timer);
}

/* Normalises out-of-range fields (month 13, second 75, ...) the way mktime does, then rewrites them. */
LIBC_FN int64_t sysv_timegm(Tm *tm)
{
    int64_t months = (int64_t)tm->tm_year * 12 + tm->tm_mon;
    int64_t year = 1900 + (months >= 0 ? months / 12 : -((-months + 11) / 12));
    unsigned month = (unsigned)(months - (year - 1900) * 12) + 1;
    int64_t days = time_days_from_civil(year, month, 1) + (tm->tm_mday - 1);
    int64_t seconds = days * SECONDS_PER_DAY + (int64_t)tm->tm_hour * 3600 + (int64_t)tm->tm_min * 60 + tm->tm_sec;
    sysv_gmtime_r(&seconds, tm);
    return seconds;
}

LIBC_FN int64_t sysv_mktime(Tm *tm)
{
    return sysv_timegm(tm);
}
