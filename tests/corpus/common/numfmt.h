/*
 * File: tests/corpus/common/numfmt.h
 * Purpose: Libc-free decimal formatting shared by corpus programs, so their output depends only on puts
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_NUMFMT_H
#define CROSSPORT_NUMFMT_H

#include <stddef.h>

#define NUMFMT_DECIMAL_BASE 10
#define NUMFMT_MAX_DIGITS 24

/* Writes `value` in decimal to `out` (NUL terminated, truncated to `capacity`); returns the length written. */
static inline size_t format_long(char *out, size_t capacity, long value)
{
    if (capacity == 0) {
        return 0;
    }
    char digits[NUMFMT_MAX_DIGITS];
    size_t count = 0;
    unsigned long magnitude = value < 0 ? 0UL - (unsigned long)value : (unsigned long)value;
    do {
        digits[count++] = (char)('0' + magnitude % NUMFMT_DECIMAL_BASE);
        magnitude /= NUMFMT_DECIMAL_BASE;
    } while (magnitude != 0);

    size_t length = 0;
    if (value < 0 && length + 1 < capacity) {
        out[length++] = '-';
    }
    while (count > 0 && length + 1 < capacity) {
        out[length++] = digits[--count];
    }
    out[length] = '\0';
    return length;
}

#endif
