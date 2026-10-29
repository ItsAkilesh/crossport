/*
 * File: shim/locale_obj.c
 * Purpose: locale_t objects, nl_langinfo and the locale constants (all in the one built-in locale)
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The shim has a single locale: the C locale with UTF-8 text, spelled "C.UTF-8" by glibc. Objects
 * created with newlocale are therefore all alike; they exist so that C++ runtimes, which pass a
 * locale_t to every call, have something to hold and free.
 */

#include <string.h>

#include "libc_abi.h"
#include "thread_cell.h"

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

static ThreadCell g_locale_cell = THREAD_CELL_INIT;

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
    void *current = thread_cell_get(&g_locale_cell);
    void *previous = current != NULL ? current : LC_GLOBAL_LOCALE;
    if (locale != NULL) {
        thread_cell_set(&g_locale_cell, locale);
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

LIBC_FN char *sysv_bind_textdomain_codeset(const char *domain, const char *codeset)
{
    (void)domain;
    return (char *)codeset;
}
