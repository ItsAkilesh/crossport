/*
 * File: shim/rand48.c
 * Purpose: The drand48 family: a 48-bit linear congruential generator
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define RAND48_MULTIPLIER 0x5deece66dull
#define RAND48_ADDEND 0xbull
#define RAND48_MASK 0xffffffffffffull
#define RAND48_SEED_LOW 0x330eull
#define RAND48_BITS 48

typedef struct {
    int quotient;
    int remainder;
} DivT;

typedef struct {
    int64_t quotient;
    int64_t remainder;
} LDivT;

/* The 48-bit linear congruential generator of drand48 and its relatives. */
static uint64_t g_state = (0x1234abcdull << 16) | RAND48_SEED_LOW;

static uint64_t advance(uint64_t *state)
{
    *state = (*state * RAND48_MULTIPLIER + RAND48_ADDEND) & RAND48_MASK;
    return *state;
}

static uint64_t from_words(const uint16_t words[3])
{
    return ((uint64_t)words[2] << 32) | ((uint64_t)words[1] << 16) | words[0];
}

static void to_words(uint64_t state, uint16_t words[3])
{
    words[0] = (uint16_t)state;
    words[1] = (uint16_t)(state >> 16);
    words[2] = (uint16_t)(state >> 32);
}

static double to_unit_interval(uint64_t state)
{
    return (double)state / (double)(1ull << RAND48_BITS);
}

LIBC_FN void sysv_srand48(int64_t seed)
{
    g_state = (((uint64_t)seed & 0xffffffffull) << 16) | RAND48_SEED_LOW;
}

LIBC_FN double sysv_drand48(void)
{
    return to_unit_interval(advance(&g_state));
}

LIBC_FN int64_t sysv_lrand48(void)
{
    return (int64_t)(advance(&g_state) >> 17);
}

LIBC_FN int64_t sysv_mrand48(void)
{
    return (int32_t)(advance(&g_state) >> 16);
}

LIBC_FN double sysv_erand48(uint16_t words[3])
{
    uint64_t state = from_words(words);
    advance(&state);
    to_words(state, words);
    return to_unit_interval(state);
}

LIBC_FN int64_t sysv_nrand48(uint16_t words[3])
{
    uint64_t state = from_words(words);
    advance(&state);
    to_words(state, words);
    return (int64_t)(state >> 17);
}

LIBC_FN int64_t sysv_jrand48(uint16_t words[3])
{
    uint64_t state = from_words(words);
    advance(&state);
    to_words(state, words);
    return (int32_t)(state >> 16);
}
