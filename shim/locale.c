/*
 * File: shim/locale.c
 * Purpose: The C/POSIX locale: setlocale and localeconv with glibc's struct layout
 * Author: Akilesh
 * Date: 2026-10-28
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

/* Only the C locale exists. "" selects the environment's locale, which the corpus pins to C. */
LIBC_FN char *sysv_setlocale(int category, const char *locale)
{
    (void)category;
    if (locale == NULL || *locale == '\0' || lstrcmpA(locale, "C") == 0 || lstrcmpA(locale, "POSIX") == 0) {
        return g_c_locale;
    }
    return NULL;
}

LIBC_FN Lconv *sysv_localeconv(void)
{
    return &g_lconv;
}
