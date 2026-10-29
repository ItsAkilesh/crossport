/*
 * File: shim/ctype.c
 * Purpose: glibc's ctype lookup tables for the C locale; <ctype.h> macros index them directly
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * The tables cover indices -128..255 so that both signed chars and EOF (-1) are valid indices.
 * The class bits are glibc's little-endian layout (__ISbit in <ctype.h>).
 */

#include "libc_abi.h"

enum {
    IS_UPPER = 0x100, IS_LOWER = 0x200, IS_ALPHA = 0x400, IS_DIGIT = 0x800, IS_XDIGIT = 0x1000,
    IS_SPACE = 0x2000, IS_PRINT = 0x4000, IS_GRAPH = 0x8000, IS_BLANK = 0x1, IS_CNTRL = 0x2,
    IS_PUNCT = 0x4, IS_ALNUM = 0x8
};

#define TABLE_OFFSET 128
#define TABLE_SIZE 384
#define ASCII_LIMIT 128

static uint16_t g_classes[TABLE_SIZE];
static int32_t g_lower[TABLE_SIZE];
static int32_t g_upper[TABLE_SIZE];
static const uint16_t *g_classes_pointer = g_classes + TABLE_OFFSET;
static const int32_t *g_lower_pointer = g_lower + TABLE_OFFSET;
static const int32_t *g_upper_pointer = g_upper + TABLE_OFFSET;
static volatile LONG g_ready;

static uint16_t classify(int c)
{
    uint16_t bits = 0;
    int is_upper = c >= 'A' && c <= 'Z';
    int is_lower = c >= 'a' && c <= 'z';
    int is_digit = c >= '0' && c <= '9';
    int is_space = c == ' ' || (c >= '\t' && c <= '\r');
    int is_print = c >= ' ' && c < 0x7f;
    int is_alpha = is_upper || is_lower;
    int is_graph = is_print && c != ' ';
    bits |= is_upper ? IS_UPPER : 0;
    bits |= is_lower ? IS_LOWER : 0;
    bits |= is_alpha ? IS_ALPHA : 0;
    bits |= is_digit ? IS_DIGIT : 0;
    bits |= (is_digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) ? IS_XDIGIT : 0;
    bits |= is_space ? IS_SPACE : 0;
    bits |= is_print ? IS_PRINT : 0;
    bits |= is_graph ? IS_GRAPH : 0;
    bits |= (c == ' ' || c == '\t') ? IS_BLANK : 0;
    bits |= (c < ' ' || c == 0x7f) ? IS_CNTRL : 0;
    bits |= (is_graph && !is_alpha && !is_digit) ? IS_PUNCT : 0;
    bits |= (is_alpha || is_digit) ? IS_ALNUM : 0;
    return bits;
}

static void ensure_tables(void)
{
    if (g_ready) {
        return;
    }
    for (int c = -TABLE_OFFSET; c < TABLE_SIZE - TABLE_OFFSET; c++) {
        int is_ascii = c >= 0 && c < ASCII_LIMIT;
        g_classes[c + TABLE_OFFSET] = is_ascii ? classify(c) : 0;
        g_lower[c + TABLE_OFFSET] = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
        g_upper[c + TABLE_OFFSET] = (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
    }
    InterlockedExchange(&g_ready, 1);
}

LIBC_FN const uint16_t **sysv___ctype_b_loc(void)
{
    ensure_tables();
    return &g_classes_pointer;
}

LIBC_FN const int32_t **sysv___ctype_tolower_loc(void)
{
    ensure_tables();
    return &g_lower_pointer;
}

LIBC_FN const int32_t **sysv___ctype_toupper_loc(void)
{
    ensure_tables();
    return &g_upper_pointer;
}
