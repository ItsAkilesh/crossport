/*
 * File: tests/corpus/two_libs/main.c
 * Purpose: Milestone 2 corpus program; uses two bundled shared libraries and prints evidence of every cross-module path
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

#include "libgreet.h"
#include "libmath.h"

#define NUMBER_BUFFER_SIZE 32
#define EXPECTED_TOTAL 20

static void print_number(const char *label, long value)
{
    char text[NUMBER_BUFFER_SIZE];
    puts(label);
    math_format(text, sizeof(text), value);
    puts(text);
}

int main(void)
{
    greet_run();
    print_number("counter:", math_counter);
    print_number("total:", greet_total());
    math_bump();
    print_number("counter after bump:", math_counter);
    return greet_total() == EXPECTED_TOTAL ? 0 : 1;
}
