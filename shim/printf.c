/*
 * File: shim/printf.c
 * Purpose: The printf formatting engine over a System V va_list, and the printf, snprintf and fortify entry points
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Every function here is SYSV because the argument list comes from translated code and is a System V
 * va_list. Integers, strings, pointers and %c are formatted here to match glibc exactly; floating-point
 * conversions are delegated to mingw's correctly rounding __mingw_snprintf.
 */

#include <math.h>

#include "printf_internal.h"

int __mingw_snprintf(char *buffer, size_t size, const char *format, ...);

#define NUMBER_BUFFER 80
#define FLOAT_STACK_BUFFER 512

typedef struct {
    int is_left, is_plus, is_space, is_alternate, is_zero;
    int width;
    int precision; /* -1 when absent */
    int length;    /* 0 int, 1 long, 2 long long, 3 long double, -1 short, -2 char */
} Spec;

static SYSV int emit(Out *out, const char *data, size_t count)
{
    out->total += count;
    if (out->file != NULL) {
        return file_put(out->file, data, count);
    }
    if (out->capacity > 0 && out->written + 1 < out->capacity) {
        size_t room = out->capacity - 1 - out->written;
        size_t take = count < room ? count : room;
        CopyMemory(out->buffer + out->written, data, take);
        out->written += take;
    }
    return 0;
}

static SYSV int emit_repeat(Out *out, char c, int count)
{
    char block[32];
    memset(block, c, sizeof(block));
    while (count > 0) {
        int take = count < (int)sizeof(block) ? count : (int)sizeof(block);
        if (emit(out, block, (size_t)take) != 0) {
            return -1;
        }
        count -= take;
    }
    return 0;
}

/* Pads `body` (with an optional leading `prefix` such as a sign) to the field width. */
static SYSV int pad_field(Out *out, const Spec *spec, const char *prefix, const char *body, size_t body_length, int zero_ok)
{
    size_t prefix_length = strlen(prefix);
    int pad = spec->width - (int)(prefix_length + body_length);
    int zeros = (spec->is_zero && zero_ok && !spec->is_left && pad > 0) ? pad : 0;
    int spaces = (pad > 0 && !zeros && !spec->is_left) ? pad : 0;
    if (emit_repeat(out, ' ', spaces) || emit(out, prefix, prefix_length) || emit_repeat(out, '0', zeros) ||
        emit(out, body, body_length)) {
        return -1;
    }
    return spec->is_left ? emit_repeat(out, ' ', pad > 0 ? pad : 0) : 0;
}

static SYSV int format_integer(Out *out, const Spec *spec, unsigned long long value, int is_negative, int base, int upper)
{
    char digits[NUMBER_BUFFER];
    size_t count = 0;
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int is_zero_without_digits = value == 0 && spec->precision == 0;
    for (unsigned long long rest = value; rest != 0 && !is_zero_without_digits; rest /= (unsigned)base) {
        digits[count++] = alphabet[rest % (unsigned)base];
    }
    char body[NUMBER_BUFFER + 8];
    size_t length = 0;
    int zeros_needed = spec->precision > (int)count ? spec->precision - (int)count : 0;
    if (spec->precision < 0 && count == 0 && !is_zero_without_digits) {
        digits[count++] = '0';
    }
    if (base == 8 && spec->is_alternate && (zeros_needed == 0) && (count == 0 || digits[count - 1] != '0')) {
        body[length++] = '0';
    }
    while (zeros_needed-- > 0) {
        body[length++] = '0';
    }
    while (count > 0) {
        body[length++] = digits[--count];
    }
    char prefix[4] = "";
    if (is_negative) {
        strcpy(prefix, "-");
    } else if (base == 10 && spec->is_plus) {
        strcpy(prefix, "+");
    } else if (base == 10 && spec->is_space) {
        strcpy(prefix, " ");
    } else if (base == 16 && spec->is_alternate && value != 0) {
        strcpy(prefix, upper ? "0X" : "0x");
    }
    return pad_field(out, spec, prefix, body, length, spec->precision < 0);
}

static SYSV unsigned long long fetch_unsigned(const Spec *spec, __builtin_sysv_va_list ap)
{
    switch (spec->length) {
    case 1:
    case 2:
        return __builtin_va_arg(ap, unsigned long long);
    case -1:
        return (unsigned short)__builtin_va_arg(ap, unsigned);
    case -2:
        return (unsigned char)__builtin_va_arg(ap, unsigned);
    default:
        return __builtin_va_arg(ap, unsigned);
    }
}

static SYSV long long fetch_signed(const Spec *spec, __builtin_sysv_va_list ap)
{
    switch (spec->length) {
    case 1:
    case 2:
        return __builtin_va_arg(ap, long long);
    case -1:
        return (short)__builtin_va_arg(ap, int);
    case -2:
        return (signed char)__builtin_va_arg(ap, int);
    default:
        return __builtin_va_arg(ap, int);
    }
}

static SYSV int format_string(Out *out, const Spec *spec, const char *text)
{
    size_t length = text == NULL ? 0 : strlen(text);
    if (text == NULL) {
        text = (spec->precision < 0 || spec->precision >= 6) ? "(null)" : "";
        length = strlen(text);
    }
    if (spec->precision >= 0 && (size_t)spec->precision < length) {
        length = (size_t)spec->precision;
    }
    return pad_field(out, &(Spec){.is_left = spec->is_left, .width = spec->width}, "", text, length, 0);
}

static SYSV int format_pointer(Out *out, const Spec *spec, uintptr_t value)
{
    if (value == 0) {
        return pad_field(out, &(Spec){.is_left = spec->is_left, .width = spec->width}, "", "(nil)", 5, 0);
    }
    Spec hex = *spec;
    hex.is_alternate = 1;
    return format_integer(out, &hex, value, 0, 16, 0);
}

/* inf and nan are written here: glibc prints "-nan" for a NaN with the sign bit set. */
static SYSV int format_special(Out *out, const Spec *spec, long double value, int upper)
{
    const char *text = isnan(value) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
    const char *sign = signbit(value) ? "-" : (spec->is_plus ? "+" : (spec->is_space ? " " : ""));
    return pad_field(out, spec, sign, text, strlen(text), 0);
}

static SYSV int format_float(Out *out, const Spec *spec, char conversion, __builtin_sysv_va_list ap)
{
    long double value = spec->length == 3 ? __builtin_va_arg(ap, long double) : (long double)__builtin_va_arg(ap, double);
    int upper = conversion == 'F' || conversion == 'E' || conversion == 'G' || conversion == 'A';
    if (isnan(value) || isinf(value)) {
        return format_special(out, spec, value, upper);
    }
    char format[32];
    char *at = format;
    *at++ = '%';
    if (spec->is_left) *at++ = '-';
    if (spec->is_plus) *at++ = '+';
    if (spec->is_space) *at++ = ' ';
    if (spec->is_alternate) *at++ = '#';
    if (spec->is_zero) *at++ = '0';
    at += wsprintfA(at, "%d", spec->width);
    if (spec->precision >= 0) {
        at += wsprintfA(at, ".%d", spec->precision);
    }
    if (spec->length == 3) *at++ = 'L';
    *at++ = conversion;
    *at = '\0';
    char stack[FLOAT_STACK_BUFFER];
    char *buffer = stack;
    int needed = spec->length == 3 ? __mingw_snprintf(stack, sizeof(stack), format, value)
                                    : __mingw_snprintf(stack, sizeof(stack), format, (double)value);
    if (needed >= (int)sizeof(stack)) {
        buffer = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)needed + 1);
        if (buffer == NULL) {
            return -1;
        }
        needed = spec->length == 3 ? __mingw_snprintf(buffer, (size_t)needed + 1, format, value)
                                    : __mingw_snprintf(buffer, (size_t)needed + 1, format, (double)value);
    }
    int result = needed < 0 ? -1 : emit(out, buffer, (size_t)needed);
    if (buffer != stack) {
        HeapFree(GetProcessHeap(), 0, buffer);
    }
    return result;
}

static SYSV const char *parse_spec(const char *at, Spec *spec, __builtin_sysv_va_list ap)
{
    memset(spec, 0, sizeof(*spec));
    spec->precision = -1;
    for (;; at++) {
        if (*at == '-') spec->is_left = 1;
        else if (*at == '+') spec->is_plus = 1;
        else if (*at == ' ') spec->is_space = 1;
        else if (*at == '#') spec->is_alternate = 1;
        else if (*at == '0') spec->is_zero = 1;
        else if (*at != '\'') break;
    }
    if (*at == '*') {
        spec->width = __builtin_va_arg(ap, int);
        if (spec->width < 0) {
            spec->is_left = 1;
            spec->width = -spec->width;
        }
        at++;
    } else {
        for (; *at >= '0' && *at <= '9'; at++) spec->width = spec->width * 10 + (*at - '0');
    }
    if (*at == '.') {
        at++;
        spec->precision = 0;
        if (*at == '*') {
            spec->precision = __builtin_va_arg(ap, int);
            at++;
        } else {
            for (; *at >= '0' && *at <= '9'; at++) spec->precision = spec->precision * 10 + (*at - '0');
        }
    }
    if (*at == 'h') {
        spec->length = at[1] == 'h' ? -2 : -1;
        at += at[1] == 'h' ? 2 : 1;
    } else if (*at == 'l') {
        spec->length = at[1] == 'l' ? 2 : 1;
        at += at[1] == 'l' ? 2 : 1;
    } else if (*at == 'q' || *at == 'j' || *at == 'z' || *at == 't') {
        spec->length = 2;
        at++;
    } else if (*at == 'L') {
        spec->length = 3;
        at++;
    }
    return at;
}

static SYSV int format_one(Out *out, const Spec *spec, char conversion, __builtin_sysv_va_list ap)
{
    char scratch[48];
    switch (conversion) {
    case 'd':
    case 'i': {
        long long value = fetch_signed(spec, ap);
        return format_integer(out, spec, value < 0 ? 0ull - (unsigned long long)value : (unsigned long long)value, value < 0, 10, 0);
    }
    case 'u': return format_integer(out, spec, fetch_unsigned(spec, ap), 0, 10, 0);
    case 'o': return format_integer(out, spec, fetch_unsigned(spec, ap), 0, 8, 0);
    case 'x': return format_integer(out, spec, fetch_unsigned(spec, ap), 0, 16, 0);
    case 'X': return format_integer(out, spec, fetch_unsigned(spec, ap), 0, 16, 1);
    case 'c': {
        char c = (char)__builtin_va_arg(ap, int);
        return pad_field(out, &(Spec){.is_left = spec->is_left, .width = spec->width}, "", &c, 1, 0);
    }
    case 's': return format_string(out, spec, __builtin_va_arg(ap, const char *));
    case 'p': return format_pointer(out, spec, (uintptr_t)__builtin_va_arg(ap, void *));
    case 'm': return format_string(out, spec, shim_strerror(*shim_errno_ptr(), scratch, sizeof(scratch)));
    case 'n': {
        int *target = __builtin_va_arg(ap, int *);
        *target = (int)out->total;
        return 0;
    }
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
        return format_float(out, spec, conversion, ap);
    default:
        return -1;
    }
}

SYSV int printf_core(Out *out, const char *format, __builtin_sysv_va_list ap)
{
    for (const char *at = format; *at != '\0';) {
        const char *percent = strchr(at, '%');
        size_t literal = percent != NULL ? (size_t)(percent - at) : strlen(at);
        if (literal > 0 && emit(out, at, literal) != 0) {
            return -1;
        }
        if (percent == NULL) {
            break;
        }
        if (percent[1] == '%') {
            if (emit(out, "%", 1) != 0) {
                return -1;
            }
            at = percent + 2;
            continue;
        }
        Spec spec;
        const char *conversion = parse_spec(percent + 1, &spec, ap);
        if (*conversion == '\0' || format_one(out, &spec, *conversion, ap) != 0) {
            return -1;
        }
        at = conversion + 1;
    }
    return (int)out->total;
}
