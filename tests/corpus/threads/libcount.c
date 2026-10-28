/*
 * File: tests/corpus/threads/libcount.c
 * Purpose: Per-thread counter in a PIC library (general-dynamic TLS, initialised data)
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "libcount.h"

#define COUNTER_START 1000

static __thread long counter = COUNTER_START;

long count_add(long amount)
{
    counter += amount;
    return counter;
}
