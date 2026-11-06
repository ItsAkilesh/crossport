/*
 * File: tests/corpus/ifunc/libpick.c
 * Purpose: A shared library with GNU indirect functions: an exported one, a local one (R_X86_64_IRELATIVE) and a variadic one
 * Author: Akilesh
 * Date: 2026-11-06
 */
#include <cpuid.h>
#include <stdarg.h>
#include <stdio.h>

#define SSE2_BONUS 1000
#define CPUID_FEATURES_LEAF 1
#define CPUID_EDX_SSE2 (1u << 26)
#define LOCAL_BONUS 2000

int pick_scale(int value);
int pick_local(int value);
int pick_format(char *out, unsigned long size, const char *format, ...);

static int scale_plain(int value)
{
    return value * 2;
}

static int scale_with_bonus(int value)
{
    return value * 2 + SSE2_BONUS;
}

/* Every x86-64 CPU has SSE2, so the choice is the same on each machine, but it is made by running cpuid. */
static int (*resolve_scale(void))(int)
{
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(CPUID_FEATURES_LEAF, &eax, &ebx, &ecx, &edx) && (edx & CPUID_EDX_SSE2) != 0) {
        return scale_with_bonus;
    }
    return scale_plain;
}

static int local_bonus(int value)
{
    return value + LOCAL_BONUS;
}

static int (*resolve_local(void))(int)
{
    return local_bonus;
}

static int format_message(char *out, unsigned long size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out, size, format, args);
    va_end(args);
    return written;
}

static int (*resolve_format(void))(char *, unsigned long, const char *, ...)
{
    return format_message;
}

int pick_scale(int value) __attribute__((ifunc("resolve_scale")));
int pick_format(char *out, unsigned long size, const char *format, ...) __attribute__((ifunc("resolve_format")));
static int pick_local_ifunc(int value) __attribute__((ifunc("resolve_local")));

int pick_local(int value)
{
    return pick_local_ifunc(value);
}
