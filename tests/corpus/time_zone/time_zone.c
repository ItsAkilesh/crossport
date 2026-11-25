/*
 * File: tests/corpus/time_zone/time_zone.c
 * Purpose: Compares local time conversion with glibc for TZ set to UTC and to fixed POSIX zones: localtime, mktime, strftime %z and %Z,
 *          ctime, and the tzset variables
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SAMPLE_TIME 1700000000 /* 2023-11-14 22:13:20 UTC */
#define OLD_TIME 86400
#define TEXT_BYTES 64

static void show_zone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
    time_t moment = SAMPLE_TIME;
    struct tm local;
    localtime_r(&moment, &local);
    char text[TEXT_BYTES];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %z %Z", &local);
    printf("TZ=%-9s [%s] gmtoff %ld isdst %d wday %d yday %d\n", tz, text, local.tm_gmtoff, local.tm_isdst, local.tm_wday, local.tm_yday);
    printf("  timezone %ld daylight %d tzname %s/%s\n", timezone, daylight, tzname[0], tzname[1]);
    struct tm back = local;
    time_t round_trip = mktime(&back);
    printf("  mktime round trip %s\n", round_trip == moment ? "exact" : "DIFFERENT");
    struct tm wall = {.tm_year = 2024 - 1900, .tm_mon = 1, .tm_mday = 29, .tm_hour = 12, .tm_isdst = -1};
    time_t made = mktime(&wall);
    printf("  2024-02-29 12:00 local is %ld UTC; normalised wday %d yday %d\n", (long)made, wall.tm_wday, wall.tm_yday);
    struct tm overflow = {.tm_year = 70, .tm_mon = 12, .tm_mday = 32, .tm_hour = 25, .tm_isdst = -1};
    time_t fixed = mktime(&overflow);
    printf("  overflowing fields normalise to %04d-%02d-%02d %02d:%02d (%ld)\n", overflow.tm_year + 1900, overflow.tm_mon + 1, overflow.tm_mday,
           overflow.tm_hour, overflow.tm_min, (long)fixed);
    time_t old = OLD_TIME;
    printf("  ctime of one day after the epoch: %s", ctime(&old));
}

int main(void)
{
    show_zone("UTC");
    show_zone("EST5");
    show_zone("CET-1");
    show_zone("IST-5:30");
    show_zone("PST8");
    return 0;
}
