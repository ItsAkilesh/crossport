/*
 * File: shim/fnmatch.c
 * Purpose: fnmatch: shell wildcard matching
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define FNM_PATHNAME 1
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4
#define FNM_CASEFOLD 16
#define FNM_NOMATCH 1

/* fnmatch: shell wildcard matching (*, ?, [...] with ranges and negation, backslash escapes). */
static int match_set(const char *pattern, int c, int flags, const char **after)
{
    const char *at = pattern + 1;
    int is_negated = *at == '!' || *at == '^';
    int is_matched = 0;
    at += is_negated;
    int is_first = 1;
    while (*at != '\0' && (*at != ']' || is_first)) {
        int low = (unsigned char)*at;
        int high = low;
        if (at[1] == '-' && at[2] != ']' && at[2] != '\0') {
            high = (unsigned char)at[2];
            at += 2;
        }
        int folded = (flags & FNM_CASEFOLD) && c >= 'A' && c <= 'Z' ? c + 32 : c;
        is_matched |= (c >= low && c <= high) || (folded >= low && folded <= high);
        at++;
        is_first = 0;
    }
    *after = *at == ']' ? at + 1 : at;
    return is_matched != is_negated;
}

static int fnmatch_from(const char *pattern, const char *text, int flags, int is_start)
{
    for (; *pattern != '\0'; pattern++) {
        int is_leading_dot = (flags & FNM_PERIOD) && *text == '.' && (is_start || ((flags & FNM_PATHNAME) && text[-1] == '/'));
        switch (*pattern) {
        case '?':
            if (*text == '\0' || is_leading_dot || ((flags & FNM_PATHNAME) && *text == '/')) {
                return FNM_NOMATCH;
            }
            text++;
            break;
        case '*':
            if (is_leading_dot) {
                return FNM_NOMATCH;
            }
            while (pattern[1] == '*') {
                pattern++;
            }
            for (const char *tail = text;; tail++) {
                if (fnmatch_from(pattern + 1, tail, flags, 0) == 0) {
                    return 0;
                }
                if (*tail == '\0' || ((flags & FNM_PATHNAME) && *tail == '/')) {
                    return FNM_NOMATCH;
                }
            }
        case '[': {
            const char *after;
            if (*text == '\0' || is_leading_dot || !match_set(pattern, (unsigned char)*text, flags, &after)) {
                return FNM_NOMATCH;
            }
            text++;
            pattern = after - 1;
            break;
        }
        case '\\':
            if (!(flags & FNM_NOESCAPE) && pattern[1] != '\0') {
                pattern++;
            }
            /* fall through */
        default: {
            int a = (unsigned char)*pattern;
            int b = (unsigned char)*text;
            if ((flags & FNM_CASEFOLD) && a >= 'A' && a <= 'Z') {
                a += 32;
            }
            if ((flags & FNM_CASEFOLD) && b >= 'A' && b <= 'Z') {
                b += 32;
            }
            if (a != b) {
                return FNM_NOMATCH;
            }
            text++;
        }
        }
        is_start = 0;
    }
    return *text == '\0' ? 0 : FNM_NOMATCH;
}

LIBC_FN int sysv_fnmatch(const char *pattern, const char *text, int flags)
{
    return fnmatch_from(pattern, text, flags, 1);
}
