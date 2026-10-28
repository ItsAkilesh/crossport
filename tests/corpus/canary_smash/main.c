/*
 * File: tests/corpus/canary_smash/main.c
 * Purpose: Overflows a stack buffer on purpose; with a working stack protector the process must abort, not return
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

#define BUFFER_SIZE 16
#define OVERFLOW_BYTES 128
#define FILL_BYTE 0x41

/* The volatile cursor stops the compiler from seeing (and removing) the overflow. */
static void smash(void)
{
    char buffer[BUFFER_SIZE];
    volatile char *cursor = buffer;
    for (int i = 0; i < OVERFLOW_BYTES; i++) {
        cursor[i] = (char)FILL_BYTE;
    }
}

int main(void)
{
    puts("before the overflow");
    smash();
    puts("after the overflow: the stack protector failed");
    return 0;
}
