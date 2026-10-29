/*
 * File: shim/scan.c
 * Purpose: sscanf family: formatted input from a string, with glibc's conversions and return values
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Supports %d %i %u %o %x %X %c %s %[set] %e %f %g %a %p %n %% with the *, width and hh h l ll L j z t
 * modifiers. Stream-based scanf and fscanf are not provided yet.
 */

#include <stdlib.h>
#include <string.h>

#include "libc_abi.h"

#define NO_WIDTH (1 << 20)
#define FLOAT_TOKEN_MAX 127
#define SET_TABLE_SIZE 256
#define LENGTH_CHAR (-2)
#define LENGTH_SHORT (-1)
#define LENGTH_LONG 1
#define LENGTH_LONG_LONG 2
#define LENGTH_LONG_DOUBLE 3

typedef struct {
    const char *start;
    const char *cursor;
} Input;

typedef struct {
    int is_suppressed;
    int width;
    int length;
    char conversion;
} Spec;

enum Outcome { MATCH_FAILED = -1, MATCHED_SKIPPED = 0, MATCHED_STORED = 1 };

static int is_space(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;
}

static void skip_space(Input *in)
{
    while (is_space((unsigned char)*in->cursor)) {
        in->cursor++;
    }
}

static int parse_length(const char **format)
{
    const char *f = *format;
    int length = 0;
    switch (*f) {
    case 'h': f++; length = *f == 'h' ? (f++, LENGTH_CHAR) : LENGTH_SHORT; break;
    case 'l': f++; length = *f == 'l' ? (f++, LENGTH_LONG_LONG) : LENGTH_LONG; break;
    case 'L': f++; length = LENGTH_LONG_DOUBLE; break;
    case 'q': f++; length = LENGTH_LONG_LONG; break;
    case 'j': case 'z': case 't': f++; length = LENGTH_LONG; break;
    default: break;
    }
    *format = f;
    return length;
}

/* Parses what follows '%'. Returns the format position after the conversion character. */
static const char *parse_spec(const char *format, Spec *spec)
{
    *spec = (Spec){.width = NO_WIDTH};
    if (*format == '*') {
        spec->is_suppressed = 1;
        format++;
    }
    if (*format >= '0' && *format <= '9') {
        int width = 0;
        for (; *format >= '0' && *format <= '9'; format++) {
            width = width < NO_WIDTH ? width * 10 + (*format - '0') : width;
        }
        spec->width = width > 0 && width < NO_WIDTH ? width : NO_WIDTH;
    }
    spec->length = parse_length(&format);
    spec->conversion = *format;
    return *format != '\0' ? format + 1 : format;
}

/* Fills `table` from the text after '[' and returns the position after ']'. */
static const char *parse_set(const char *format, unsigned char *table)
{
    int is_negated = *format == '^';
    format += is_negated;
    memset(table, 0, SET_TABLE_SIZE);
    int is_first = 1;
    for (; *format != '\0' && (*format != ']' || is_first); format++, is_first = 0) {
        unsigned char low = (unsigned char)*format;
        unsigned char high = low;
        if (format[1] == '-' && format[2] != ']' && format[2] != '\0') {
            high = (unsigned char)format[2];
            format += 2;
        }
        for (int c = low; c <= high; c++) {
            table[c] = 1;
        }
    }
    if (is_negated) {
        for (int c = 0; c < SET_TABLE_SIZE; c++) {
            table[c] = !table[c];
        }
    }
    return *format == ']' ? format + 1 : format;
}

static void store_integer(const Spec *spec, __builtin_sysv_va_list ap, uint64_t value)
{
    void *destination = __builtin_va_arg(ap, void *);
    switch (spec->length) {
    case LENGTH_CHAR: *(uint8_t *)destination = (uint8_t)value; break;
    case LENGTH_SHORT: *(uint16_t *)destination = (uint16_t)value; break;
    case 0: *(uint32_t *)destination = (uint32_t)value; break;
    default: *(uint64_t *)destination = value; break;
    }
}

static int read_sign(const char **p, int *width)
{
    int is_negative = 0;
    if (*width > 0 && (**p == '-' || **p == '+')) {
        is_negative = **p == '-';
        (*p)++;
        (*width)--;
    }
    return is_negative;
}

static int scan_integer(Input *in, int width, int base, uint64_t *value)
{
    const char *p = in->cursor;
    int is_negative = read_sign(&p, &width);
    if ((base == 0 || base == 16) && width > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_value(p[2]) < 16) {
        base = 16;
        p += 2;
        width -= 2;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    uint64_t accumulated = 0;
    int count = 0;
    for (; width > 0 && digit_value((unsigned char)*p) < base; p++, width--, count++) {
        accumulated = accumulated * (uint64_t)base + (uint64_t)digit_value((unsigned char)*p);
    }
    if (count == 0) {
        return 0;
    }
    *value = is_negative ? (uint64_t)0 - accumulated : accumulated;
    in->cursor = p;
    return 1;
}

static int convert_integer(Input *in, const Spec *spec, __builtin_sysv_va_list ap)
{
    int base = 10;
    switch (spec->conversion) {
    case 'i': base = 0; break;
    case 'o': base = 8; break;
    case 'x': case 'X': case 'p': base = 16; break;
    default: break;
    }
    skip_space(in);
    uint64_t value = 0;
    if (!scan_integer(in, spec->width, base, &value)) {
        return MATCH_FAILED;
    }
    if (spec->is_suppressed) {
        return MATCHED_SKIPPED;
    }
    store_integer(spec, ap, value);
    return MATCHED_STORED;
}

static int convert_float(Input *in, const Spec *spec, __builtin_sysv_va_list ap)
{
    char token[FLOAT_TOKEN_MAX + 1];
    skip_space(in);
    size_t length = 0;
    while (length < FLOAT_TOKEN_MAX && (int)length < spec->width && in->cursor[length] != '\0' && !is_space((unsigned char)in->cursor[length])) {
        token[length] = in->cursor[length];
        length++;
    }
    token[length] = '\0';
    char *end = NULL;
    long double value = strtold(token, &end);
    if (end == token) {
        return MATCH_FAILED;
    }
    in->cursor += end - token;
    if (spec->is_suppressed) {
        return MATCHED_SKIPPED;
    }
    void *destination = __builtin_va_arg(ap, void *);
    if (spec->length == LENGTH_LONG_DOUBLE) {
        *(long double *)destination = value;
    } else if (spec->length == LENGTH_LONG) {
        *(double *)destination = (double)strtod(token, NULL);
    } else {
        *(float *)destination = (float)value;
    }
    return MATCHED_STORED;
}

/* %s, %c and %[: copies characters in `table`, or any non-space characters when it is NULL. */
static int convert_text(Input *in, const Spec *spec, const unsigned char *table, __builtin_sysv_va_list ap)
{
    int is_character = spec->conversion == 'c';
    int width = is_character && spec->width == NO_WIDTH ? 1 : spec->width;
    if (spec->conversion == 's') {
        skip_space(in);
    }
    const char *p = in->cursor;
    while (width > 0 && *p != '\0' && (is_character || (table != NULL ? table[(unsigned char)*p] : !is_space((unsigned char)*p)))) {
        p++;
        width--;
    }
    size_t length = (size_t)(p - in->cursor);
    size_t required = is_character ? (size_t)(spec->width == NO_WIDTH ? 1 : spec->width) : 1;
    if (length < required) {
        return MATCH_FAILED;
    }
    if (!spec->is_suppressed) {
        char *destination = __builtin_va_arg(ap, char *);
        memcpy(destination, in->cursor, length);
        if (!is_character) {
            destination[length] = '\0';
        }
    }
    in->cursor = p;
    return spec->is_suppressed ? MATCHED_SKIPPED : MATCHED_STORED;
}

static int convert_count(const Input *in, const Spec *spec, __builtin_sysv_va_list ap)
{
    if (spec->is_suppressed) {
        return MATCHED_SKIPPED;
    }
    store_integer(spec, ap, (uint64_t)(in->cursor - in->start));
    return MATCHED_SKIPPED;
}

static int convert(Input *in, const Spec *spec, const unsigned char *table, __builtin_sysv_va_list ap)
{
    switch (spec->conversion) {
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p':
        return convert_integer(in, spec, ap);
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
        return convert_float(in, spec, ap);
    case 's': case 'c': case '[':
        return convert_text(in, spec, table, ap);
    case 'n':
        return convert_count(in, spec, ap);
    default:
        return MATCH_FAILED;
    }
}

/* Runs one directive at `*format`. Returns the Outcome and advances `*format` past it. */
static int run_directive(Input *in, const char **format, __builtin_sysv_va_list ap)
{
    unsigned char table[SET_TABLE_SIZE];
    Spec spec;
    *format = parse_spec(*format + 1, &spec);
    if (spec.conversion == '%') {
        skip_space(in);
        return *in->cursor++ == '%' ? MATCHED_SKIPPED : MATCH_FAILED;
    }
    const unsigned char *accepted = NULL;
    if (spec.conversion == '[') {
        *format = parse_set(*format, table);
        accepted = table;
    }
    return convert(in, &spec, accepted, ap);
}

static int scan_core(const char *text, const char *format, __builtin_sysv_va_list ap)
{
    Input in = {.start = text, .cursor = text};
    int assigned = 0;
    while (*format != '\0') {
        if (is_space((unsigned char)*format)) {
            skip_space(&in);
            format++;
            continue;
        }
        if (*format != '%') {
            if (*in.cursor++ != *format++) {
                break;
            }
            continue;
        }
        int outcome = run_directive(&in, &format, ap);
        if (outcome == MATCH_FAILED) {
            return assigned == 0 && *in.cursor == '\0' ? -1 : assigned;
        }
        assigned += outcome;
    }
    return assigned;
}

LIBC_FN int sysv_vsscanf(const char *text, const char *format, __builtin_sysv_va_list ap)
{
    return scan_core(text, format, ap);
}

LIBC_FN int sysv_sscanf(const char *text, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = scan_core(text, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_sscanf(const char *text, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = scan_core(text, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc23_sscanf(const char *text, const char *format, ...)
{
    __builtin_sysv_va_list ap;
    __builtin_sysv_va_start(ap, format);
    int result = scan_core(text, format, ap);
    __builtin_sysv_va_end(ap);
    return result;
}

LIBC_FN int sysv___isoc99_vsscanf(const char *text, const char *format, __builtin_sysv_va_list ap)
{
    return scan_core(text, format, ap);
}
