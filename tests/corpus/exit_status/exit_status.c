/*
 * File: tests/corpus/exit_status/exit_status.c
 * Purpose: Ends with an exit status above 255: Linux keeps only the low eight bits, so the translated program must exit with 44
 * Author: Akilesh
 * Date: 2026-11-24
 */

#include <stdio.h>
#include <stdlib.h>

#define OUT_OF_RANGE_STATUS 300

int main(void)
{
    printf("exiting with %d, which a shell sees as %d\n", OUT_OF_RANGE_STATUS, OUT_OF_RANGE_STATUS & 0xff);
    exit(OUT_OF_RANGE_STATUS);
}
