/*
 * File: tests/corpus/hello_dyn/hello_dyn.c
 * Purpose: Milestone 1 corpus program; a dynamically linked ELF that only calls puts
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

int main(void)
{
    puts("hello from translated crossport");
    return 0;
}
