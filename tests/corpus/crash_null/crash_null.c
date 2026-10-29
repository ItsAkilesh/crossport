/*
 * File: tests/corpus/crash_null/crash_null.c
 * Purpose: Writes through a null pointer on purpose; the shim must report the crash on stderr before the process ends
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdio.h>

/* noinline keeps the faulting store in a frame of its own, so the report names this function's code. */
static __attribute__((noinline)) void crash(volatile int *target)
{
    *target = 1;
}

int main(void)
{
    puts("before the crash");
    fflush(stdout);
    crash(NULL);
    puts("after the crash: the program continued");
    return 0;
}
