/*
 * File: tests/corpus/two_libs/libgreet.c
 * Purpose: Second bundled library; calls into libmath across modules, uses its table and has its own constructor and destructor
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

#include "libgreet.h"
#include "libmath.h"

#define FIRST_OPERAND 2
#define SECOND_OPERAND 3
#define MULTIPLIER 4
#define MULTIPLY_INDEX 1

static void greet_ctor(void) __attribute__((constructor));
static void greet_dtor(void) __attribute__((destructor));

static void greet_ctor(void)
{
    puts("ctor: libgreet");
}

static void greet_dtor(void)
{
    puts("dtor: libgreet");
}

void greet_run(void)
{
    puts("greet: hello from libgreet");
    math_bump();
    math_bump();
}

int greet_total(void)
{
    return math_ops[MULTIPLY_INDEX](math_add(FIRST_OPERAND, SECOND_OPERAND), MULTIPLIER);
}
