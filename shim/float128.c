/*
 * File: shim/float128.c
 * Purpose: strtof128 and strfromf128, which newer libstdc++ builds name even for programs that never use __float128
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Quad precision is carried through long double and double, so values past their range or precision are
 * rounded. That is enough for what a C++ runtime does with them (from_chars, to_chars).
 */

#include <string.h>

#include "libc_abi.h"

#define FORMAT_BYTES 16

LIBC_FN long double sysv_strtold(const char *text, char **end);
LIBC_FN int sysv_snprintf(char *buffer, size_t size, const char *format, ...);

LIBC_FN __float128 sysv_strtof128(const char *text, char **end)
{
    return (__float128)sysv_strtold(text, end);
}

/* The format is "%[.precision]conversion", the only form strfrom accepts. */
LIBC_FN int sysv_strfromf128(char *buffer, size_t size, const char *format, __float128 value)
{
    char converted[FORMAT_BYTES];
    size_t length = 0;
    if (format[0] != '%' || strlen(format) >= sizeof(converted) - 1) {
        return shim_fail(LINUX_EINVAL);
    }
    converted[length++] = '%';
    for (const char *at = format + 1; *at != '\0'; at++) {
        converted[length++] = *at;
    }
    converted[length] = '\0';
    return sysv_snprintf(buffer, size, converted, (double)value);
}
