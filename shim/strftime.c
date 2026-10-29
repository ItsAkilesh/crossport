/*
 * File: shim/strftime.c
 * Purpose: strftime and strptime for the C locale, including glibc's - _ 0 ^ flags and field widths
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "time_internal.h"

#define ABBREVIATION_LENGTH 3
#define TWO_DIGITS 2

static const char *const g_days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const g_months[] = {"January", "February", "March", "April", "May", "June", "July",
                                       "August", "September", "October", "November", "December"};

typedef struct {
    char *out;
    size_t size;
    size_t length;
    int has_overflowed;
} Writer;

static void put(Writer *w, const char *text, size_t count)
{
    if (w->length + count + 1 > w->size) {
        w->has_overflowed = 1;
        return;
    }
    CopyMemory(w->out + w->length, text, count);
    w->length += count;
}

static void put_text(Writer *w, const char *text, size_t limit, char flag)
{
    for (size_t i = 0; text[i] != '\0' && i < limit; i++) {
        char c = text[i];
        if ((flag == '^' || flag == '#') && c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        put(w, &c, 1);
    }
}

/* A number padded with `fill` to `digits` (or the explicit `width`); flags - _ 0 override the default fill. */
static void put_number(Writer *w, long long value, int digits, char fill, char flag, int width)
{
    char text[32];
    unsigned long long magnitude = value < 0 ? 0ull - (unsigned long long)value : (unsigned long long)value;
    int count = wsprintfA(text, "%I64u", magnitude);
    char pad = flag == '_' ? ' ' : (flag == '0' ? '0' : fill);
    int wanted = flag == '-' ? 0 : (width > digits ? width : digits);
    if (value < 0) {
        put(w, "-", 1);
    }
    for (int i = count; i < wanted; i++) {
        put(w, &pad, 1);
    }
    put(w, text, (size_t)count);
}

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/* ISO 8601 week number; also yields the ISO year, which differs near year boundaries. */
static int iso_week(const Tm *tm, int *iso_year)
{
    int weekday = (tm->tm_wday + 6) % 7; /* Monday = 0 */
    int year = tm->tm_year + 1900;
    int week = (tm->tm_yday - weekday + 10) / 7;
    *iso_year = year;
    if (week < 1) {
        *iso_year = year - 1;
        week = (tm->tm_yday + (is_leap(year - 1) ? 366 : 365) - weekday + 10) / 7;
    } else if (week == 53 && tm->tm_yday - weekday + 3 >= (is_leap(year) ? 366 : 365)) {
        week = 1;
        *iso_year = year + 1;
    }
    return week;
}

static void format_one(Writer *w, char conversion, char flag, int width, const Tm *tm);

static void put_pattern(Writer *w, const char *pattern, const Tm *tm)
{
    for (const char *at = pattern; *at != '\0'; at++) {
        if (*at == '%' && at[1] != '\0') {
            format_one(w, *++at, 0, 0, tm);
        } else {
            put(w, at, 1);
        }
    }
}

static void format_one(Writer *w, char conversion, char flag, int width, const Tm *tm)
{
    int weekday = (tm->tm_wday >= 0 && tm->tm_wday < 7) ? tm->tm_wday : 0;
    int month = (tm->tm_mon >= 0 && tm->tm_mon < 12) ? tm->tm_mon : 0;
    int hour12 = tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12;
    int iso_year;
    switch (conversion) {
    case 'a': put_text(w, g_days[weekday], ABBREVIATION_LENGTH, flag); break;
    case 'A': put_text(w, g_days[weekday], 16, flag); break;
    case 'b': case 'h': put_text(w, g_months[month], ABBREVIATION_LENGTH, flag); break;
    case 'B': put_text(w, g_months[month], 16, flag); break;
    case 'c': put_pattern(w, "%a %b %e %H:%M:%S %Y", tm); break;
    case 'C': put_number(w, (tm->tm_year + 1900) / 100, 2, '0', flag, width); break;
    case 'd': put_number(w, tm->tm_mday, 2, '0', flag, width); break;
    case 'D': case 'x': put_pattern(w, "%m/%d/%y", tm); break;
    case 'e': put_number(w, tm->tm_mday, 2, ' ', flag, width); break;
    case 'F': put_pattern(w, "%Y-%m-%d", tm); break;
    case 'g': iso_week(tm, &iso_year); put_number(w, iso_year % 100, 2, '0', flag, width); break;
    case 'G': iso_week(tm, &iso_year); put_number(w, iso_year, 1, '0', flag, width); break;
    case 'H': put_number(w, tm->tm_hour, 2, '0', flag, width); break;
    case 'I': put_number(w, hour12, 2, '0', flag, width); break;
    case 'j': put_number(w, tm->tm_yday + 1, 3, '0', flag, width); break;
    case 'k': put_number(w, tm->tm_hour, 2, ' ', flag, width); break;
    case 'l': put_number(w, hour12, 2, ' ', flag, width); break;
    case 'm': put_number(w, tm->tm_mon + 1, 2, '0', flag, width); break;
    case 'M': put_number(w, tm->tm_min, 2, '0', flag, width); break;
    case 'n': put(w, "\n", 1); break;
    case 'p': put_text(w, tm->tm_hour < 12 ? "AM" : "PM", 2, flag == '#' ? 0 : flag); break;
    case 'P': put(w, tm->tm_hour < 12 ? "am" : "pm", 2); break;
    case 'r': put_pattern(w, "%I:%M:%S %p", tm); break;
    case 'R': put_pattern(w, "%H:%M", tm); break;
    case 's': {
        Tm copy = *tm;
        put_number(w, sysv_timegm(&copy) - tm->tm_gmtoff, 1, '0', flag, width);
        break;
    }
    case 'S': put_number(w, tm->tm_sec, 2, '0', flag, width); break;
    case 't': put(w, "\t", 1); break;
    case 'T': case 'X': put_pattern(w, "%H:%M:%S", tm); break;
    case 'u': put_number(w, weekday == 0 ? 7 : weekday, 1, '0', flag, width); break;
    case 'U': put_number(w, (tm->tm_yday + 7 - weekday) / 7, 2, '0', flag, width); break;
    case 'V': put_number(w, iso_week(tm, &iso_year), 2, '0', flag, width); break;
    case 'w': put_number(w, weekday, 1, '0', flag, width); break;
    case 'W': put_number(w, (tm->tm_yday + 7 - (weekday + 6) % 7) / 7, 2, '0', flag, width); break;
    case 'y': put_number(w, (tm->tm_year + 1900) % 100, 2, '0', flag, width); break;
    case 'Y': put_number(w, tm->tm_year + 1900, 1, '0', flag, width); break;
    case 'z': {
        long offset = tm->tm_gmtoff / 60;
        put(w, offset < 0 ? "-" : "+", 1);
        put_number(w, (offset < 0 ? -offset : offset) / 60 * 100 + (offset < 0 ? -offset : offset) % 60, 4, '0', 0, 0);
        break;
    }
    case 'Z': put_text(w, tm->tm_zone != NULL ? tm->tm_zone : "", 64, flag); break;
    case '%': put(w, "%", 1); break;
    default: {
        char literal[3] = {'%', conversion, '\0'};
        put(w, literal, 2);
    }
    }
}

LIBC_FN size_t sysv_strftime(char *buffer, size_t size, const char *format, const Tm *tm)
{
    Writer w = {buffer, size, 0, 0};
    for (const char *at = format; *at != '\0'; at++) {
        if (*at != '%') {
            put(&w, at, 1);
            continue;
        }
        char flag = 0;
        int width = 0;
        for (at++; *at == '-' || *at == '_' || *at == '0' || *at == '^' || *at == '#'; at++) {
            flag = *at;
        }
        for (; *at >= '0' && *at <= '9'; at++) {
            width = width * 10 + (*at - '0');
        }
        if (*at == 'E' || *at == 'O') {
            at++;
        }
        if (*at == '\0') {
            break;
        }
        format_one(&w, *at, flag, width, tm);
    }
    if (w.has_overflowed || size == 0) {
        return 0;
    }
    buffer[w.length] = '\0';
    return w.length;
}

static int fold(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int matches_name(const char *text, const char *name, size_t *consumed)
{
    size_t full = strlen(name);
    size_t i = 0;
    while (i < full && fold((uint8_t)text[i]) == fold((uint8_t)name[i])) {
        i++;
    }
    if (i == full || i >= ABBREVIATION_LENGTH) {
        *consumed = i == full ? full : ABBREVIATION_LENGTH;
        return i == full || fold((uint8_t)text[ABBREVIATION_LENGTH]) < 'a' || fold((uint8_t)text[ABBREVIATION_LENGTH]) > 'z';
    }
    return 0;
}

static const char *parse_name(const char *at, const char *const *names, int count, int *index)
{
    for (int i = 0; i < count; i++) {
        size_t consumed;
        if (matches_name(at, names[i], &consumed)) {
            *index = i;
            return at + consumed;
        }
    }
    return NULL;
}

static const char *parse_number(const char *at, int max_digits, int low, int high, int *out)
{
    int value = 0;
    int digits = 0;
    while (*at == ' ' && digits == 0) {
        at++;
    }
    for (; digits < max_digits && *at >= '0' && *at <= '9'; at++, digits++) {
        value = value * 10 + (*at - '0');
    }
    if (digits == 0 || value < low || value > high) {
        return NULL;
    }
    *out = value;
    return at;
}

static const char *parse_pattern(const char *at, const char *pattern, Tm *tm);

static const char *parse_conversion(const char *at, char conversion, Tm *tm)
{
    int value;
    switch (conversion) {
    case 'Y': at = parse_number(at, 4, 0, 9999, &value); if (at) tm->tm_year = value - 1900; return at;
    case 'm': at = parse_number(at, 2, 1, 12, &value); if (at) tm->tm_mon = value - 1; return at;
    case 'd': case 'e': at = parse_number(at, 2, 1, 31, &value); if (at) tm->tm_mday = value; return at;
    case 'H': case 'k': at = parse_number(at, 2, 0, 23, &value); if (at) tm->tm_hour = value; return at;
    case 'I': case 'l': at = parse_number(at, 2, 1, 12, &value); if (at) tm->tm_hour = value % 12; return at;
    case 'M': at = parse_number(at, 2, 0, 59, &value); if (at) tm->tm_min = value; return at;
    case 'S': at = parse_number(at, 2, 0, 61, &value); if (at) tm->tm_sec = value; return at;
    case 'y': at = parse_number(at, 2, 0, 99, &value); if (at) tm->tm_year = value < 69 ? value + 100 : value; return at;
    case 'j': at = parse_number(at, 3, 1, 366, &value); if (at) tm->tm_yday = value - 1; return at;
    case 'b': case 'B': case 'h': at = parse_name(at, g_months, 12, &value); if (at) tm->tm_mon = value; return at;
    case 'a': case 'A': return parse_name(at, g_days, 7, &tm->tm_wday);
    case 'p': case 'P':
        if (fold((uint8_t)at[1]) != 'm') return NULL;
        if (fold((uint8_t)at[0]) == 'p') tm->tm_hour = tm->tm_hour % 12 + 12;
        else if (fold((uint8_t)at[0]) != 'a') return NULL;
        return at + 2;
    case 'T': return parse_pattern(at, "%H:%M:%S", tm);
    case 'R': return parse_pattern(at, "%H:%M", tm);
    case 'D': case 'x': return parse_pattern(at, "%m/%d/%y", tm);
    case 'F': return parse_pattern(at, "%Y-%m-%d", tm);
    case 'X': return parse_pattern(at, "%H:%M:%S", tm);
    case 'c': return parse_pattern(at, "%a %b %e %H:%M:%S %Y", tm);
    case 'r': return parse_pattern(at, "%I:%M:%S %p", tm);
    case 'Z': while ((*at >= 'A' && *at <= 'Z') || (*at >= 'a' && *at <= 'z')) at++; return at;
    case 'z': {
        if (*at == 'Z') return at + 1;
        int sign = *at == '-' ? -1 : 1;
        if (*at != '+' && *at != '-') return NULL;
        int hours, minutes = 0;
        at = parse_number(at + 1, 2, 0, 23, &hours);
        if (at != NULL && *at == ':') at++;
        if (at != NULL && *at >= '0' && *at <= '9') at = parse_number(at, 2, 0, 59, &minutes);
        if (at) tm->tm_gmtoff = sign * (hours * 3600L + minutes * 60L);
        return at;
    }
    case 's': {
        char *end;
        long long seconds = strtoll(at, &end, 10);
        if (end == at) return NULL;
        int64_t timer = seconds;
        return sysv_gmtime_r(&timer, tm) != NULL ? end : NULL;
    }
    case '%': return *at == '%' ? at + 1 : NULL;
    default: return NULL;
    }
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static const char *skip_space(const char *at)
{
    while (is_blank(*at)) {
        at++;
    }
    return at;
}

static const char *parse_pattern(const char *at, const char *pattern, Tm *tm)
{
    for (const char *f = pattern; *f != '\0' && at != NULL; f++) {
        if (is_blank(*f)) {
            at = skip_space(at);
        } else if (*f == '%') {
            f++;
            if (*f == 'E' || *f == 'O') {
                f++;
            }
            at = (*f == 'n' || *f == 't') ? skip_space(at) : parse_conversion(at, *f, tm);
        } else if (*at == *f) {
            at++;
        } else {
            return NULL;
        }
    }
    return at;
}

/* Fills tm_wday and tm_yday from the date fields, as glibc's strptime does when a full date was read. */
static void complete_date(Tm *tm)
{
    int64_t year = tm->tm_year + 1900;
    int64_t days = time_days_from_civil(year, (unsigned)tm->tm_mon + 1, (unsigned)(tm->tm_mday > 0 ? tm->tm_mday : 1));
    tm->tm_wday = (int)(((days % 7) + 11) % 7);
    tm->tm_yday = (int)(days - time_days_from_civil(year, 1, 1));
}

LIBC_FN char *sysv_strptime(const char *text, const char *format, Tm *tm)
{
    const char *end = parse_pattern(text, format, tm);
    if (end == NULL) {
        return NULL;
    }
    complete_date(tm);
    return (char *)end;
}
