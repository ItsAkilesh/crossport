/*
 * File: tests/corpus/ifunc/main.c
 * Purpose: Call GNU indirect functions through the PLT, through a function pointer and from inside their own library
 * Author: Akilesh
 * Date: 2026-11-06
 */
#include <stdio.h>

int pick_scale(int value);
int pick_local(int value);
int pick_format(char *out, unsigned long size, const char *format, ...);

int main(void)
{
    int (*scale)(int) = pick_scale;
    char text[64];
    printf("scale %d %d\n", pick_scale(21), scale(5));
    printf("local %d\n", pick_local(7));
    pick_format(text, sizeof text, "%d-%s-%.2f", 42, "x", 1.5);
    printf("format %s\n", text);
    return 0;
}
