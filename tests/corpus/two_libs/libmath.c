/*
 * File: tests/corpus/two_libs/libmath.c
 * Purpose: First bundled library; exports functions, a data symbol, a function-pointer table, a constructor and a destructor
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

#include "libmath.h"

#define DECIMAL_BASE 10
#define MAX_DIGITS 24

int math_counter = 0;

static void math_ctor(void) __attribute__((constructor));
static void math_dtor(void) __attribute__((destructor));

static void math_ctor(void)
{
    puts("ctor: libmath");
}

static void math_dtor(void)
{
    puts("dtor: libmath");
}

int math_add(int a, int b)
{
    return a + b;
}

int math_mul(int a, int b)
{
    return a * b;
}

void math_bump(void)
{
    math_counter++;
}

/* Exported symbols are preemptible, so this table needs R_X86_64_64 relocations, not RELATIVE ones. */
const math_op_fn math_ops[] = {math_add, math_mul};
const int math_op_count = 2;

size_t math_format(char *out, size_t capacity, long value)
{
    if (capacity == 0) {
        return 0;
    }
    char digits[MAX_DIGITS];
    size_t count = 0;
    unsigned long magnitude = value < 0 ? 0UL - (unsigned long)value : (unsigned long)value;
    do {
        digits[count++] = (char)('0' + magnitude % DECIMAL_BASE);
        magnitude /= DECIMAL_BASE;
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
