/*
 * File: shim/locale_obj.c
 * Purpose: locale_t objects, nl_langinfo, and the *_l variants of the locale-sensitive functions (all in the one built-in locale)
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The shim has a single locale: the C locale with UTF-8 text, spelled "C.UTF-8" by glibc. Objects
 * created with newlocale are therefore all alike; they exist so that C++ runtimes, which pass a
 * locale_t to every call, have something to hold and free.
 */

#include <string.h>

#include "libc_abi.h"

#define LC_GLOBAL_LOCALE ((void *)-1)
#define MB_CUR_MAX_UTF8 6
#define ITEM_CATEGORY_SHIFT 16
#define LANGINFO_CODESET 14
#define LANGINFO_TIME_FIRST 0x20000
#define LANGINFO_ABDAY_FIRST 0x20000
#define LANGINFO_DAY_FIRST 0x20007
#define LANGINFO_ABMON_FIRST 0x2000e
#define LANGINFO_MON_FIRST 0x2001a
#define LANGINFO_AM_STR 0x20026
#define LANGINFO_PM_STR 0x20027
#define LANGINFO_D_T_FMT 0x20028
#define LANGINFO_D_FMT 0x20029
#define LANGINFO_T_FMT 0x2002a
#define LANGINFO_T_FMT_AMPM 0x2002b
#define LANGINFO_RADIXCHAR 0x10000
#define LANGINFO_YESEXPR 0x50000
#define LANGINFO_NOEXPR 0x50001
#define LANGINFO_CRNCYSTR 0x4000f
#define DAYS_PER_WEEK 7
#define MONTHS_PER_YEAR 12

typedef struct {
    int mask;
} Locale;

static const char *const ABBREVIATED_DAYS[DAYS_PER_WEEK] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const DAYS[DAYS_PER_WEEK] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const ABBREVIATED_MONTHS[MONTHS_PER_YEAR] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const MONTHS[MONTHS_PER_YEAR] = {"January", "February", "March", "April", "May", "June",
                                                    "July", "August", "September", "October", "November", "December"};

static __thread void *t_current_locale = LC_GLOBAL_LOCALE;

LIBC_FN size_t sysv_strftime(char *out, size_t size, const char *format, const void *tm);
LIBC_FN double sysv_strtod(const char *text, char **end);
LIBC_FN float sysv_strtof(const char *text, char **end);
LIBC_FN long double sysv_strtold(const char *text, char **end);
LIBC_FN int sysv_strcoll(const char *first, const char *second);
LIBC_FN size_t sysv_mbstowcs(int32_t *out, const char *source, size_t limit);
LIBC_FN size_t sysv_wcstombs(char *out, const int32_t *source, size_t limit);
LIBC_FN uint32_t sysv_towlower(uint32_t c);
LIBC_FN uint32_t sysv_towupper(uint32_t c);
LIBC_FN uint64_t sysv_wctype(const char *name);
LIBC_FN int sysv_iswctype(uint32_t c, uint64_t kind);
LIBC_FN int sysv_wcscmp(const int32_t *first, const int32_t *second);
LIBC_FN size_t sysv_wcslen(const int32_t *text);

/* True for the names every locale in this shim answers to. */
static int is_known_name(const char *name)
{
    size_t length = strlen(name);
    int is_c = strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0 || strncmp(name, "C.", 2) == 0 || length == 0;
    int is_utf8 = length > 5 && (strcmp(name + length - 5, "UTF-8") == 0 || strcmp(name + length - 5, "utf-8") == 0 ||
                                 strcmp(name + length - 4, "utf8") == 0);
    return is_c || is_utf8;
}

LIBC_FN void *sysv_newlocale(int mask, const char *name, void *base)
{
    if (name == NULL || !is_known_name(name)) {
        shim_set_errno(LINUX_ENOENT);
        return NULL;
    }
    Locale *locale = base != NULL && base != LC_GLOBAL_LOCALE ? (Locale *)base : (Locale *)HeapAlloc(GetProcessHeap(), 0, sizeof(Locale));
    if (locale == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    locale->mask = mask;
    return locale;
}

LIBC_FN void *sysv_duplocale(void *locale)
{
    return locale == LC_GLOBAL_LOCALE ? sysv_newlocale(0, "C", NULL) : sysv_newlocale(((Locale *)locale)->mask, "C", NULL);
}

LIBC_FN void sysv_freelocale(void *locale)
{
    if (locale != NULL && locale != LC_GLOBAL_LOCALE) {
        HeapFree(GetProcessHeap(), 0, locale);
    }
}

/* Makes `locale` this thread's locale (NULL only asks) and returns the previous one. */
LIBC_FN void *sysv_uselocale(void *locale)
{
    void *previous = t_current_locale;
    if (locale != NULL) {
        t_current_locale = locale;
    }
    return previous;
}

LIBC_FN void *sysv___newlocale(int mask, const char *name, void *base)
{
    return sysv_newlocale(mask, name, base);
}

LIBC_FN void *sysv___duplocale(void *locale)
{
    return sysv_duplocale(locale);
}

LIBC_FN void sysv___freelocale(void *locale)
{
    sysv_freelocale(locale);
}

LIBC_FN void *sysv___uselocale(void *locale)
{
    return sysv_uselocale(locale);
}

LIBC_FN size_t sysv___ctype_get_mb_cur_max(void)
{
    return MB_CUR_MAX_UTF8;
}

static const char *time_item(int item)
{
    if (item >= LANGINFO_ABDAY_FIRST && item < LANGINFO_ABDAY_FIRST + DAYS_PER_WEEK) {
        return ABBREVIATED_DAYS[item - LANGINFO_ABDAY_FIRST];
    }
    if (item >= LANGINFO_DAY_FIRST && item < LANGINFO_DAY_FIRST + DAYS_PER_WEEK) {
        return DAYS[item - LANGINFO_DAY_FIRST];
    }
    if (item >= LANGINFO_ABMON_FIRST && item < LANGINFO_ABMON_FIRST + MONTHS_PER_YEAR) {
        return ABBREVIATED_MONTHS[item - LANGINFO_ABMON_FIRST];
    }
    if (item >= LANGINFO_MON_FIRST && item < LANGINFO_MON_FIRST + MONTHS_PER_YEAR) {
        return MONTHS[item - LANGINFO_MON_FIRST];
    }
    switch (item) {
    case LANGINFO_AM_STR:
        return "AM";
    case LANGINFO_PM_STR:
        return "PM";
    case LANGINFO_D_T_FMT:
        return "%a %b %e %H:%M:%S %Y";
    case LANGINFO_D_FMT:
        return "%m/%d/%y";
    case LANGINFO_T_FMT:
        return "%H:%M:%S";
    case LANGINFO_T_FMT_AMPM:
        return "%I:%M:%S %p";
    default:
        return "";
    }
}

LIBC_FN char *sysv_nl_langinfo(int item)
{
    if (item == LANGINFO_CODESET) {
        return (char *)"UTF-8";
    }
    if (item == LANGINFO_RADIXCHAR) {
        return (char *)".";
    }
    if (item == LANGINFO_YESEXPR) {
        return (char *)"^[yY]";
    }
    if (item == LANGINFO_NOEXPR) {
        return (char *)"^[nN]";
    }
    if (item == LANGINFO_CRNCYSTR) {
        return (char *)"-";
    }
    return (char *)((item >> ITEM_CATEGORY_SHIFT) == (LANGINFO_TIME_FIRST >> ITEM_CATEGORY_SHIFT) ? time_item(item) : "");
}

LIBC_FN char *sysv___nl_langinfo_l(int item, void *locale)
{
    (void)locale;
    return sysv_nl_langinfo(item);
}

LIBC_FN size_t sysv_strxfrm(char *to, const char *from, size_t size)
{
    size_t length = strlen(from);
    if (size > 0) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(to, from, copied);
        to[copied] = '\0';
    }
    return length;
}

LIBC_FN int sysv___strcoll_l(const char *first, const char *second, void *locale)
{
    (void)locale;
    return sysv_strcoll(first, second);
}

LIBC_FN size_t sysv___strxfrm_l(char *to, const char *from, size_t size, void *locale)
{
    (void)locale;
    return sysv_strxfrm(to, from, size);
}

LIBC_FN size_t sysv___strftime_l(char *out, size_t size, const char *format, const void *tm, void *locale)
{
    (void)locale;
    return sysv_strftime(out, size, format, tm);
}

LIBC_FN double sysv___strtod_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtod(text, end);
}

LIBC_FN float sysv___strtof_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtof(text, end);
}

LIBC_FN long double sysv___strtold_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtold(text, end);
}

LIBC_FN uint32_t sysv___towlower_l(uint32_t c, void *locale)
{
    (void)locale;
    return sysv_towlower(c);
}

LIBC_FN uint32_t sysv___towupper_l(uint32_t c, void *locale)
{
    (void)locale;
    return sysv_towupper(c);
}

LIBC_FN uint64_t sysv___wctype_l(const char *name, void *locale)
{
    (void)locale;
    return sysv_wctype(name);
}

LIBC_FN int sysv___iswctype_l(uint32_t c, uint64_t kind, void *locale)
{
    (void)locale;
    return sysv_iswctype(c, kind);
}

LIBC_FN int sysv_wcscoll(const int32_t *first, const int32_t *second)
{
    return sysv_wcscmp(first, second);
}

LIBC_FN int sysv___wcscoll_l(const int32_t *first, const int32_t *second, void *locale)
{
    (void)locale;
    return sysv_wcscmp(first, second);
}

LIBC_FN size_t sysv_wcsxfrm(int32_t *to, const int32_t *from, size_t size)
{
    size_t length = sysv_wcslen(from);
    if (size > 0) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(to, from, copied * sizeof(int32_t));
        to[copied] = 0;
    }
    return length;
}

LIBC_FN size_t sysv___wcsxfrm_l(int32_t *to, const int32_t *from, size_t size, void *locale)
{
    (void)locale;
    return sysv_wcsxfrm(to, from, size);
}

/* strftime on the narrow form of the format, with the result widened again. */
LIBC_FN size_t sysv_wcsftime(int32_t *out, size_t size, const int32_t *format, const void *tm)
{
    char narrow_format[256];
    char narrow_out[512];
    if (sysv_wcstombs(narrow_format, format, sizeof(narrow_format)) >= sizeof(narrow_format)) {
        return 0;
    }
    size_t length = sysv_strftime(narrow_out, sizeof(narrow_out), narrow_format, tm);
    if (length == 0 || length + 1 > size) {
        return 0;
    }
    narrow_out[length] = '\0';
    return sysv_mbstowcs(out, narrow_out, size);
}

LIBC_FN size_t sysv___wcsftime_l(int32_t *out, size_t size, const int32_t *format, const void *tm, void *locale)
{
    (void)locale;
    return sysv_wcsftime(out, size, format, tm);
}

LIBC_FN char *sysv_bind_textdomain_codeset(const char *domain, const char *codeset)
{
    (void)domain;
    return (char *)codeset;
}

LIBC_FN double sysv_strtod_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtod(text, end);
}

LIBC_FN float sysv_strtof_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtof(text, end);
}

LIBC_FN long double sysv_strtold_l(const char *text, char **end, void *locale)
{
    (void)locale;
    return sysv_strtold(text, end);
}
