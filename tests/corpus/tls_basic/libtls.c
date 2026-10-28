/*
 * File: tests/corpus/tls_basic/libtls.c
 * Purpose: PIC library with thread-local variables: general-dynamic, local-dynamic and an exported one for initial-exec access
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "libtls.h"

#define LIB_COUNTER_START 100
#define LIB_EXPORTED_START 7

__thread int lib_counter = LIB_COUNTER_START;
__thread int lib_exported = LIB_EXPORTED_START;
static __thread int hidden_a = 1;
static __thread int hidden_b = 2;

int lib_get(void)
{
    return lib_counter;
}

void lib_set(int value)
{
    lib_counter = value;
}

int lib_read_exported(void)
{
    return lib_exported;
}

int lib_hidden_sum(void)
{
    hidden_a += 1;
    hidden_b += 2;
    return hidden_a + hidden_b;
}
