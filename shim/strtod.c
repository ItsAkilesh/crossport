/*
 * File: shim/strtod.c
 * Purpose: String to floating-point conversion (strtod family, atof) in the C locale
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * The Windows C runtime's parser is correctly rounded and accepts what glibc does in the C locale: decimal
 * and hexadecimal forms, "inf", "infinity" and "nan" in any case. It is used directly.
 */

#include <stdlib.h>

#include "libc_abi.h"

LIBC_FN double sysv_strtod(const char *text, char **end)
{
    return strtod(text, end);
}

LIBC_FN float sysv_strtof(const char *text, char **end)
{
    return strtof(text, end);
}

LIBC_FN long double sysv_strtold(const char *text, char **end)
{
    return strtold(text, end);
}

LIBC_FN double sysv_atof(const char *text)
{
    return strtod(text, NULL);
}
