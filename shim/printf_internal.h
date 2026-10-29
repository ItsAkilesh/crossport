/*
 * File: shim/printf_internal.h
 * Purpose: The output sink and entry point of the printf engine, shared by the printf family and the fortify variants
 * Author: Akilesh
 * Date: 2026-10-28
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

#endif
