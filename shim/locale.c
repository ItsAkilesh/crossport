/*
 * File: shim/locale.c
 * Purpose: The C/POSIX and C.UTF-8 locales: setlocale and localeconv with glibc's struct layout
 * Author: Akilesh
 * Date: 2026-11-17
 */

#include "libc_abi.h"

#define LC_CHAR_UNSET 127 /* CHAR_MAX: "not available in this locale" */

/* glibc's struct lconv. */
typedef struct {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    char int_p_cs_precedes;
    char int_p_sep_by_space;
    char int_n_cs_precedes;
    char int_n_sep_by_space;
    char int_p_sign_posn;
    char int_n_sign_posn;
} Lconv;

static char g_point[] = ".";
static char g_empty[] = "";
static char g_c_locale[] = "C";

static Lconv g_lconv = {
    g_point, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty,
    LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET,
    LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET, LC_CHAR_UNSET,
    LC_CHAR_UNSET, LC_CHAR_UNSET,
};

static char g_utf8_locale[] = "C.UTF-8";
static char *g_current_locale = g_c_locale;

static int is_c_utf8(const char *name)
{
    return lstrcmpiA(name, "C.UTF-8") == 0 || lstrcmpiA(name, "C.utf8") == 0;
}

LIBC_FN char *sysv_getenv(const char *name);

/* "" selects the environment's locale: LC_ALL, else LANG. Only C.UTF-8 changes the answer; any other name keeps the C locale, as before. */
static int environment_asks_for_utf8(void)
{
    const char *name = sysv_getenv("LC_ALL");
    if (name == NULL || *name == 0) {
        name = sysv_getenv("LANG");
    }
    return name != NULL && is_c_utf8(name);
}

/* The C and C.UTF-8 locales exist (glibc 2.35 and newer builds C.UTF-8 in, and the shim's one locale is UTF-8 anyway).
 * "" selects the environment's locale (C.UTF-8 when LC_ALL or LANG names it, else C). A query (NULL) returns the locale last set. */
LIBC_FN char *sysv_setlocale(int category, const char *locale)
{
    (void)category;
    if (locale == NULL) {
        return g_current_locale;
    }
    if (*locale == 0 && environment_asks_for_utf8()) {
        g_current_locale = g_utf8_locale;
        return g_current_locale;
    }
    if (*locale == '\0' || lstrcmpA(locale, "C") == 0 || lstrcmpA(locale, "POSIX") == 0) {
        g_current_locale = g_c_locale;
        return g_current_locale;
    }
    if (is_c_utf8(locale)) {
        g_current_locale = g_utf8_locale;
        return g_current_locale;
    }
    return NULL;
}

LIBC_FN Lconv *sysv_localeconv(void)
{
    return &g_lconv;
}
