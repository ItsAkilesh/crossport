/*
 * File: tests/corpus/two_libs/libmath.h
 * Purpose: Public interface of the first bundled library: arithmetic, a shared counter and an operation table
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_LIBMATH_H
#define CROSSPORT_LIBMATH_H

#include <stddef.h>

typedef int (*math_op_fn)(int, int);

extern int math_counter;
extern const math_op_fn math_ops[];
extern const int math_op_count;

int math_add(int a, int b);
int math_mul(int a, int b);
void math_bump(void);
/* Writes `value` in decimal to `out` (NUL terminated, truncated to `capacity`); returns the length written. */
size_t math_format(char *out, size_t capacity, long value);

#endif
