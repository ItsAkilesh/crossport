/*
 * File: shim/printf_internal.h
 * Purpose: The output sink and entry point of the printf engine, shared by the printf family and the fortify variants
 * Author: Akilesh
 * Date: 2026-11-24
 */

#ifndef CROSSPORT_PRINTF_INTERNAL_H
#define CROSSPORT_PRINTF_INTERNAL_H

#include <string.h>

#include "stdio_internal.h"

/* Where formatted output goes: a stream (with its lock held by the caller) or a bounded string. */
typedef struct {
    ShimFile *file;
    char *buffer;
    size_t capacity;
    size_t written;
    size_t total; /* characters the full output needs, which snprintf returns */
} Out;

/* Formats `format` with System V arguments `ap` into `out`. Returns the total length, or -1. */
SYSV int printf_core(Out *out, const char *format, __builtin_sysv_va_list ap);

/* printf_wide.c: wide arguments (%ls, %lc) as UTF-8. The string is a heap block the caller frees with HeapFree. */
size_t shim_wide_char_to_utf8(char out[4], int32_t wide);
char *shim_wide_string_to_utf8(const int32_t *text, int precision);

#endif
