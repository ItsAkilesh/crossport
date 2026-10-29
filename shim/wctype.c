/*
 * File: shim/wctype.c
 * Purpose: Character classification and case conversion: the isXXX, iswXXX, toupper and towupper families
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Narrow functions classify ASCII, as glibc's C locale does. Wide functions cover the Basic Multilingual
 * Plane through the Windows Unicode tables; code points above it are neither letters nor spaces here.
 */

#include <string.h>

#include "libc_abi.h"

#define ASCII_LIMIT 0x80
#define BMP_LIMIT 0x10000

/* glibc's wctype_t values are only meaningful to its own iswctype; these are the shim's. */
enum {
    CLASS_ALPHA = 1, CLASS_DIGIT, CLASS_ALNUM, CLASS_UPPER, CLASS_LOWER, CLASS_SPACE, CLASS_PUNCT, CLASS_PRINT,
    CLASS_GRAPH, CLASS_CNTRL, CLASS_BLANK, CLASS_XDIGIT
};

typedef struct {
    const char *name;
    uint64_t id;
} ClassName;

static const ClassName CLASS_NAMES[] = {
    {"alpha", CLASS_ALPHA}, {"digit", CLASS_DIGIT}, {"alnum", CLASS_ALNUM}, {"upper", CLASS_UPPER},
    {"lower", CLASS_LOWER}, {"space", CLASS_SPACE}, {"punct", CLASS_PUNCT}, {"print", CLASS_PRINT},
    {"graph", CLASS_GRAPH}, {"cntrl", CLASS_CNTRL}, {"blank", CLASS_BLANK}, {"xdigit", CLASS_XDIGIT},
};

LIBC_FN int sysv_toupper(int c);
LIBC_FN int sysv_tolower(int c);

static int in_range(int value, int low, int high)
{
    return value >= low && value <= high;
}

static int is_ascii_class(int c, uint64_t kind)
{
    switch (kind) {
    case CLASS_ALPHA:
        return in_range(c, 'a', 'z') || in_range(c, 'A', 'Z');
    case CLASS_DIGIT:
        return in_range(c, '0', '9');
    case CLASS_ALNUM:
        return is_ascii_class(c, CLASS_ALPHA) || is_ascii_class(c, CLASS_DIGIT);
    case CLASS_UPPER:
        return in_range(c, 'A', 'Z');
    case CLASS_LOWER:
        return in_range(c, 'a', 'z');
    case CLASS_SPACE:
        return c == ' ' || in_range(c, '\t', '\r');
    case CLASS_PUNCT:
        return in_range(c, '!', '/') || in_range(c, ':', '@') || in_range(c, '[', '`') || in_range(c, '{', '~');
    case CLASS_PRINT:
        return in_range(c, ' ', '~');
    case CLASS_GRAPH:
        return in_range(c, '!', '~');
    case CLASS_CNTRL:
        return in_range(c, 0, 0x1f) || c == 0x7f;
    case CLASS_BLANK:
        return c == ' ' || c == '\t';
    case CLASS_XDIGIT:
        return is_ascii_class(c, CLASS_DIGIT) || in_range(c, 'a', 'f') || in_range(c, 'A', 'F');
    default:
        return 0;
    }
}

#define NARROW(name, kind) \
    LIBC_FN int sysv_##name(int c) \
    { \
        return is_ascii_class(c, kind); \
    }

NARROW(isalpha, CLASS_ALPHA)
NARROW(isdigit, CLASS_DIGIT)
NARROW(isalnum, CLASS_ALNUM)
NARROW(isupper, CLASS_UPPER)
NARROW(islower, CLASS_LOWER)
NARROW(isspace, CLASS_SPACE)
NARROW(ispunct, CLASS_PUNCT)
NARROW(isprint, CLASS_PRINT)
NARROW(isgraph, CLASS_GRAPH)
NARROW(iscntrl, CLASS_CNTRL)
NARROW(isblank, CLASS_BLANK)
NARROW(isxdigit, CLASS_XDIGIT)

LIBC_FN int sysv_isascii(int c)
{
    return (c & ~0x7f) == 0;
}

/* The Windows classification bits of a BMP character (C1 type: upper, lower, digit, space, punct, cntrl, blank, xdigit, alpha). */
static WORD windows_type(uint32_t c)
{
    WORD type = 0;
    wchar_t unit = (wchar_t)c;
    return GetStringTypeW(CT_CTYPE1, &unit, 1, &type) ? type : 0;
}

static int is_wide_class(uint32_t c, uint64_t kind)
{
    if (c < ASCII_LIMIT) {
        return is_ascii_class((int)c, kind);
    }
    if (c >= BMP_LIMIT) {
        return kind == CLASS_PRINT || kind == CLASS_GRAPH;
    }
    WORD type = windows_type(c);
    switch (kind) {
    case CLASS_ALPHA:
        return (type & C1_ALPHA) != 0;
    case CLASS_ALNUM:
        return (type & (C1_ALPHA | C1_DIGIT)) != 0;
    case CLASS_UPPER:
        return (type & C1_UPPER) != 0;
    case CLASS_LOWER:
        return (type & C1_LOWER) != 0;
    case CLASS_SPACE:
        return (type & C1_SPACE) != 0;
    case CLASS_PUNCT:
        return (type & C1_PUNCT) != 0;
    case CLASS_CNTRL:
        return (type & C1_CNTRL) != 0;
    case CLASS_BLANK:
        return (type & C1_BLANK) != 0;
    case CLASS_PRINT:
        return (type & C1_CNTRL) == 0;
    case CLASS_GRAPH:
        return (type & (C1_CNTRL | C1_SPACE | C1_BLANK)) == 0;
    default:
        return 0;
    }
}

#define WIDE(name, kind) \
    LIBC_FN int sysv_##name(uint32_t c) \
    { \
        return is_wide_class(c, kind); \
    }

WIDE(iswalpha, CLASS_ALPHA)
WIDE(iswdigit, CLASS_DIGIT)
WIDE(iswalnum, CLASS_ALNUM)
WIDE(iswupper, CLASS_UPPER)
WIDE(iswlower, CLASS_LOWER)
WIDE(iswspace, CLASS_SPACE)
WIDE(iswpunct, CLASS_PUNCT)
WIDE(iswprint, CLASS_PRINT)
WIDE(iswgraph, CLASS_GRAPH)
WIDE(iswcntrl, CLASS_CNTRL)
WIDE(iswblank, CLASS_BLANK)
WIDE(iswxdigit, CLASS_XDIGIT)

LIBC_FN uint64_t sysv_wctype(const char *name)
{
    for (size_t i = 0; i < sizeof(CLASS_NAMES) / sizeof(CLASS_NAMES[0]); i++) {
        if (strcmp(name, CLASS_NAMES[i].name) == 0) {
            return CLASS_NAMES[i].id;
        }
    }
    return 0;
}

LIBC_FN int sysv_iswctype(uint32_t c, uint64_t kind)
{
    return is_wide_class(c, kind);
}

static uint32_t map_case(uint32_t c, DWORD flags)
{
    if (c < ASCII_LIMIT) {
        return flags == LCMAP_UPPERCASE ? (uint32_t)sysv_toupper((int)c) : (uint32_t)sysv_tolower((int)c);
    }
    if (c >= BMP_LIMIT) {
        return c;
    }
    wchar_t unit = (wchar_t)c;
    wchar_t mapped = unit;
    LCMapStringEx(LOCALE_NAME_INVARIANT, flags, &unit, 1, &mapped, 1, NULL, NULL, 0);
    return (uint32_t)mapped;
}

LIBC_FN uint32_t sysv_towupper(uint32_t c)
{
    return map_case(c, LCMAP_UPPERCASE);
}

LIBC_FN uint32_t sysv_towlower(uint32_t c)
{
    return map_case(c, LCMAP_LOWERCASE);
}
