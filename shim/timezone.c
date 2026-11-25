/*
 * File: shim/timezone.c
 * Purpose: The local time zone: the offset from UTC, daylight saving and the zone's abbreviation at a given moment, from TZ or from Windows
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Until now localtime and mktime treated every time as UTC. The zone is chosen the way glibc chooses it, as far as Windows allows:
 *   - TZ set to UTC, GMT or empty: UTC.
 *   - TZ set to a fixed POSIX zone such as "EST5" or "CET-1" (no daylight rule): that offset (POSIX counts west, so EST5 is UTC-5).
 *   - Anything else, including TZ unset and tz database names such as "America/New_York" (the shim carries no tz database): the Windows
 *     time zone, with its daylight saving rules at the instant asked about.
 * Windows names zones, not abbreviations, so the abbreviation comes from a short table for the common zones and is "+HH" or "+HHMM" otherwise
 * (which is what glibc prints for a zone without a letter abbreviation).
 */

#include <string.h>

#include "libc_abi.h"
#include "time_internal.h"

#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_DAY_LOCAL 86400
#define UNIX_EPOCH_YEAR 1970
#define FILETIME_TICKS_PER_SECOND 10000000ull
#define FILETIME_UNIX_EPOCH_TICKS 116444736000000000ull
#define ABBREVIATION_BYTES 16
#define NAME_CACHE_SIZE 4
#define MAX_ZONE_LETTERS 8

LIBC_FN char *sysv_getenv(const char *name);

typedef struct {
    int64_t offset_seconds;
    int is_dst;
    char abbreviation[ABBREVIATION_BYTES];
} ZoneInfo;

static char g_name_cache[NAME_CACHE_SIZE][ABBREVIATION_BYTES];
static unsigned g_name_next;

/* A copy of an abbreviation that stays valid for the life of the process (tm_zone points at it). */
static const char *remember(const char *name)
{
    for (unsigned i = 0; i < NAME_CACHE_SIZE; i++) {
        if (strcmp(g_name_cache[i], name) == 0) {
            return g_name_cache[i];
        }
    }
    char *slot = g_name_cache[g_name_next++ % NAME_CACHE_SIZE];
    lstrcpynA(slot, name, ABBREVIATION_BYTES);
    return slot;
}

static void numeric_abbreviation(int64_t offset, char *out)
{
    int64_t magnitude = offset < 0 ? -offset : offset;
    int hours = (int)(magnitude / SECONDS_PER_HOUR);
    int minutes = (int)(magnitude / SECONDS_PER_MINUTE % SECONDS_PER_MINUTE);
    if (minutes == 0) {
        wsprintfA(out, "%c%02d", offset < 0 ? '-' : '+', hours);
    } else {
        wsprintfA(out, "%c%02d%02d", offset < 0 ? '-' : '+', hours, minutes);
    }
}

static int is_utc_name(const char *name)
{
    return *name == 0 || lstrcmpA(name, "UTC") == 0 || lstrcmpA(name, "UTC0") == 0 || lstrcmpA(name, "GMT") == 0 || lstrcmpA(name, "GMT0") == 0 ||
           lstrcmpA(name, "Etc/UTC") == 0 || lstrcmpA(name, ":UTC") == 0;
}

/* TZ as a fixed POSIX zone ("EST5", "CET-1", "IST-5:30"). Returns 1 and fills `info` when it is one. */
static int parse_fixed_zone(const char *text, ZoneInfo *info)
{
    int letters = 0;
    while (letters < MAX_ZONE_LETTERS && ((text[letters] >= 'A' && text[letters] <= 'Z') || (text[letters] >= 'a' && text[letters] <= 'z'))) {
        letters++;
    }
    const char *at = text + letters;
    if (letters < 3 || (*at != '+' && *at != '-' && (*at < '0' || *at > '9'))) {
        return 0;
    }
    int sign = *at == '-' ? -1 : 1; /* POSIX counts hours west of UTC */
    at += (*at == '+' || *at == '-');
    int hours = 0;
    int minutes = 0;
    int digits = 0;
    for (; *at >= '0' && *at <= '9'; at++, digits++) {
        hours = hours * 10 + (*at - '0');
    }
    if (digits == 0 || digits > 2) {
        return 0;
    }
    if (*at == ':') {
        minutes = (at[1] - '0') * 10 + (at[2] - '0');
        at += 3;
    }
    if (*at != 0) {
        return 0; /* a daylight rule follows: not a fixed zone */
    }
    info->offset_seconds = -(int64_t)sign * (hours * SECONDS_PER_HOUR + minutes * SECONDS_PER_MINUTE);
    info->is_dst = 0;
    memcpy(info->abbreviation, text, (size_t)letters);
    info->abbreviation[letters] = 0;
    return 1;
}

static int starts_with(const wchar_t *text, const wchar_t *prefix)
{
    while (*prefix != 0) {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static int contains(const wchar_t *text, const wchar_t *part)
{
    for (; *text != 0; text++) {
        if (starts_with(text, part)) {
            return 1;
        }
    }
    return 0;
}

/* The letters people use for a Windows zone name, for the zones that have them. Returns 0 when there is no table entry. */
static int known_abbreviation(const wchar_t *windows_name, int is_dst, char *out)
{
    static const struct {
        const wchar_t *prefix;
        const char *standard;
        const char *daylight;
    } table[] = {
        {L"Eastern", "EST", "EDT"}, {L"Central Standard", "CST", "CDT"}, {L"Central Daylight", "CST", "CDT"}, {L"Mountain", "MST", "MDT"},
        {L"Pacific", "PST", "PDT"}, {L"Alaskan", "AKST", "AKDT"}, {L"Hawaiian", "HST", "HDT"}, {L"GMT", "GMT", "BST"}, {L"Greenwich", "GMT", "GMT"},
        {L"UTC", "UTC", "UTC"}, {L"India", "IST", "IST"}, {L"Tokyo", "JST", "JDT"}, {L"China", "CST", "CDT"}, {L"Korea", "KST", "KDT"},
        {L"AUS Eastern", "AEST", "AEDT"},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (starts_with(windows_name, table[i].prefix)) {
            lstrcpyA(out, is_dst ? table[i].daylight : table[i].standard);
            return 1;
        }
    }
    if (contains(windows_name, L"Europe") || contains(windows_name, L"Romance") || contains(windows_name, L"Central European")) {
        lstrcpyA(out, is_dst ? "CEST" : "CET");
        return 1;
    }
    return 0;
}

static int64_t seconds_of(const SYSTEMTIME *time)
{
    return time_days_from_civil(time->wYear, time->wMonth, time->wDay) * SECONDS_PER_DAY_LOCAL + time->wHour * SECONDS_PER_HOUR +
           time->wMinute * SECONDS_PER_MINUTE + time->wSecond;
}

static int windows_zone(int64_t utc, ZoneInfo *info)
{
    TIME_ZONE_INFORMATION zone;
    SYSTEMTIME utc_time;
    SYSTEMTIME local_time;
    FILETIME file_time;
    if (GetTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID) {
        return 0;
    }
    uint64_t ticks = (uint64_t)utc * FILETIME_TICKS_PER_SECOND + FILETIME_UNIX_EPOCH_TICKS;
    file_time.dwLowDateTime = (DWORD)ticks;
    file_time.dwHighDateTime = (DWORD)(ticks >> 32);
    if (!FileTimeToSystemTime(&file_time, &utc_time) || !SystemTimeToTzSpecificLocalTime(&zone, &utc_time, &local_time)) {
        return 0;
    }
    info->offset_seconds = seconds_of(&local_time) - seconds_of(&utc_time);
    int64_t standard_offset = -((int64_t)zone.Bias + zone.StandardBias) * SECONDS_PER_MINUTE;
    info->is_dst = zone.DaylightBias != 0 && info->offset_seconds != standard_offset;
    if (!known_abbreviation(info->is_dst ? zone.DaylightName : zone.StandardName, info->is_dst, info->abbreviation)) {
        numeric_abbreviation(info->offset_seconds, info->abbreviation);
    }
    return 1;
}

/* The zone in force at the UTC instant `utc`. Falls back to UTC if Windows cannot say. */
static void zone_at(int64_t utc, ZoneInfo *info)
{
    const char *name = sysv_getenv("TZ");
    memset(info, 0, sizeof(*info));
    lstrcpyA(info->abbreviation, "UTC");
    if (name != NULL && (is_utc_name(name) || parse_fixed_zone(name, info))) {
        if (is_utc_name(name)) {
            info->offset_seconds = 0;
        }
        return;
    }
    if (!windows_zone(utc, info)) {
        info->offset_seconds = 0;
        lstrcpyA(info->abbreviation, "UTC");
    }
}

/* Fills the zone fields of a broken-down local time for the UTC instant `utc`. */
void shim_zone_fill(int64_t utc, int *is_dst, int64_t *offset, const char **abbreviation)
{
    ZoneInfo info;
    zone_at(utc, &info);
    *is_dst = info.is_dst;
    *offset = info.offset_seconds;
    *abbreviation = remember(info.abbreviation);
}

/* The UTC instant of a local wall-clock time given as seconds since the epoch as if it were UTC: two refinements settle the offset at the boundary. */
int64_t shim_utc_from_local(int64_t local_as_utc)
{
    ZoneInfo first;
    ZoneInfo second;
    zone_at(local_as_utc, &first);
    zone_at(local_as_utc - first.offset_seconds, &second);
    return local_as_utc - second.offset_seconds;
}

/* tzset(3): the C library's zone variables for the zone in force now. */
void shim_zone_variables(int64_t *seconds_west, int *has_daylight, const char **standard, const char **daylight)
{
    int64_t now = 0;
    FILETIME file_time;
    GetSystemTimeAsFileTime(&file_time);
    now = (int64_t)((((uint64_t)file_time.dwHighDateTime << 32) | file_time.dwLowDateTime) - FILETIME_UNIX_EPOCH_TICKS) / (int64_t)FILETIME_TICKS_PER_SECOND;
    ZoneInfo january;
    ZoneInfo july;
    int64_t year_start = (now / (365 * SECONDS_PER_DAY_LOCAL)) * (365 * SECONDS_PER_DAY_LOCAL);
    zone_at(year_start, &january);
    zone_at(year_start + 182 * SECONDS_PER_DAY_LOCAL, &july);
    ZoneInfo base = january.is_dst ? july : january;
    ZoneInfo other = january.is_dst ? january : july;
    *seconds_west = -base.offset_seconds;
    *has_daylight = base.offset_seconds != other.offset_seconds;
    *standard = remember(base.abbreviation);
    *daylight = remember(*has_daylight ? other.abbreviation : base.abbreviation);
}
