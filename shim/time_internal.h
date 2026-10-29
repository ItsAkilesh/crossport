/*
 * File: shim/time_internal.h
 * Purpose: glibc's struct tm and the calendar helpers shared by time.c and strftime.c
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_TIME_INTERNAL_H
#define CROSSPORT_TIME_INTERNAL_H

#include <stdlib.h>
#include <string.h>

#include "libc_abi.h"

/* glibc's struct tm (56 bytes). */
typedef struct {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    int64_t tm_gmtoff;
    const char *tm_zone;
} Tm;

_Static_assert(sizeof(Tm) == 56, "struct tm must match glibc's layout");

int64_t time_days_from_civil(int64_t year, unsigned month, unsigned day);
LIBC_FN Tm *sysv_gmtime_r(const int64_t *timer, Tm *out);
LIBC_FN int64_t sysv_timegm(Tm *tm);

#endif
