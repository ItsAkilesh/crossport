/*
 * File: shim/stdlib.c
 * Purpose: Integer parsing, qsort (stable, like glibc 2.39), bsearch, rand (glibc's generator) and small helpers
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "libc_abi.h"

typedef int (SYSV *Comparator)(const void *, const void *);

static int is_space(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;
}

/* Reads the optional sign, whitespace and base prefix; returns the effective base and advances `cursor`. */
static int read_prefix(const char **cursor, int base, int *is_negative, int allow_binary)
{
    const char *at = *cursor;
    while (is_space((uint8_t)*at)) {
        at++;
    }
    *is_negative = (*at == '-');
    if (*at == '-' || *at == '+') {
        at++;
    }
    int has_hex = at[0] == '0' && (at[1] == 'x' || at[1] == 'X') && digit_value((uint8_t)at[2]) < 16;
    int has_binary = allow_binary && at[0] == '0' && (at[1] == 'b' || at[1] == 'B') && digit_value((uint8_t)at[2]) < 2;
    if ((base == 0 || base == 16) && has_hex) {
        at += 2;
        base = 16;
    } else if ((base == 0 || base == 2) && has_binary) {
        at += 2;
        base = 2;
    } else if (base == 0) {
        base = (at[0] == '0') ? 8 : 10;
    }
    *cursor = at;
    return base;
}

/* Shared core of strtol/strtoul and their long long and C23 forms (LP64: all 64-bit). */
static unsigned long long parse_integer(const char *text, char **end, int base, int is_signed, int allow_binary)
{
    const char *cursor = text;
    int is_negative;
    if (base != 0 && (base < 2 || base > 36)) {
        shim_set_errno(LINUX_EINVAL);
        if (end != NULL) {
            *end = (char *)text;
        }
        return 0;
    }
    base = read_prefix(&cursor, base, &is_negative, allow_binary);
    unsigned long long limit = is_signed ? (is_negative ? 0x8000000000000000ull : 0x7fffffffffffffffull) : ~0ull;
    unsigned long long value = 0;
    int has_digits = 0;
    int has_overflowed = 0;
    for (; digit_value((uint8_t)*cursor) < base; cursor++) {
        unsigned digit = (unsigned)digit_value((uint8_t)*cursor);
        has_digits = 1;
        if (value > (limit - digit) / (unsigned)base) {
            has_overflowed = 1;
        } else {
            value = value * (unsigned)base + digit;
        }
    }
    if (end != NULL) {
        *end = (char *)(has_digits ? cursor : text);
    }
    if (has_overflowed) {
        shim_set_errno(LINUX_ERANGE);
        value = limit;
    }
    return (is_negative && has_digits) ? (0ull - value) : value;
}

LIBC_FN c_long sysv_strtol(const char *text, char **end, int base)
{
    return (c_long)parse_integer(text, end, base, 1, 0);
}

LIBC_FN long long sysv_strtoll(const char *text, char **end, int base)
{
    return (long long)parse_integer(text, end, base, 1, 0);
}

LIBC_FN c_ulong sysv_strtoul(const char *text, char **end, int base)
{
    return (c_ulong)parse_integer(text, end, base, 0, 0);
}

LIBC_FN unsigned long long sysv_strtoull(const char *text, char **end, int base)
{
    return parse_integer(text, end, base, 0, 0);
}

/* glibc 2.38+ compiles C23 programs against these names; they also accept a 0b prefix. */
LIBC_FN c_long sysv___isoc23_strtol(const char *text, char **end, int base)
{
    return (c_long)parse_integer(text, end, base, 1, 1);
}

LIBC_FN long long sysv___isoc23_strtoll(const char *text, char **end, int base)
{
    return (long long)parse_integer(text, end, base, 1, 1);
}

LIBC_FN c_ulong sysv___isoc23_strtoul(const char *text, char **end, int base)
{
    return (c_ulong)parse_integer(text, end, base, 0, 1);
}

LIBC_FN unsigned long long sysv___isoc23_strtoull(const char *text, char **end, int base)
{
    return parse_integer(text, end, base, 0, 1);
}

LIBC_FN int sysv_atoi(const char *text)
{
    return (int)sysv_strtol(text, NULL, 10);
}

LIBC_FN c_long sysv_atol(const char *text)
{
    return sysv_strtol(text, NULL, 10);
}

LIBC_FN long long sysv_atoll(const char *text)
{
    return sysv_strtoll(text, NULL, 10);
}

LIBC_FN int sysv_abs(int value)
{
    return value < 0 ? -value : value;
}

LIBC_FN c_long sysv_labs(c_long value)
{
    return value < 0 ? -value : value;
}

LIBC_FN long long sysv_llabs(long long value)
{
    return value < 0 ? -value : value;
}

/* Stable top-down merge sort, the algorithm and tie-breaking of glibc 2.39's qsort. */
static void merge_sort(uint8_t *base, uint8_t *scratch, size_t count, size_t size, Comparator compare)
{
    if (count <= 1) {
        return;
    }
    size_t left_count = count / 2;
    size_t right_count = count - left_count;
    uint8_t *left = base;
    uint8_t *right = base + left_count * size;
    merge_sort(left, scratch, left_count, size, compare);
    merge_sort(right, scratch, right_count, size, compare);
    uint8_t *out = scratch;
    while (left_count > 0 && right_count > 0) {
        if (compare(left, right) <= 0) {
            CopyMemory(out, left, size);
            left += size;
            left_count--;
        } else {
            CopyMemory(out, right, size);
            right += size;
            right_count--;
        }
        out += size;
    }
    if (left_count > 0) {
        CopyMemory(out, left, left_count * size);
    }
    CopyMemory(base, scratch, (count - right_count) * size);
}

LIBC_FN void sysv_qsort(void *base, size_t count, size_t size, Comparator compare)
{
    if (count < 2 || size == 0) {
        return;
    }
    if (count > SIZE_MAX / size) {
        shim_set_errno(LINUX_ENOMEM);
        return;
    }
    uint8_t *scratch = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, count * size);
    if (scratch == NULL) {
        shim_fail(LINUX_ENOMEM);
        return;
    }
    merge_sort((uint8_t *)base, scratch, count, size, compare);
    HeapFree(GetProcessHeap(), 0, scratch);
}

LIBC_FN void *sysv_bsearch(const void *key, const void *base, size_t count, size_t size, Comparator compare)
{
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const void *candidate = (const uint8_t *)base + middle * size;
        int order = compare(key, candidate);
        if (order == 0) {
            return (void *)candidate;
        }
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return NULL;
}

/* glibc's TYPE_3 additive feedback generator: degree 31, separation 3, 310 outputs discarded after seeding. */
#define RANDOM_DEGREE 31
#define RANDOM_SEPARATION 3
#define RANDOM_DISCARD (10 * RANDOM_DEGREE)
#define RANDOM_MODULUS 2147483647
#define PARK_MILLER_MULTIPLIER 16807
#define PARK_MILLER_QUOTIENT 127773
#define PARK_MILLER_REMAINDER 2836

static int32_t g_state[RANDOM_DEGREE];
static int g_front = RANDOM_SEPARATION;
static int g_rear;
static int g_is_seeded;
static SRWLOCK g_random_lock = SRWLOCK_INIT;

static int32_t next_random(void)
{
    uint32_t sum = (uint32_t)g_state[g_front] + (uint32_t)g_state[g_rear];
    g_state[g_front] = (int32_t)sum;
    int32_t result = (int32_t)(sum >> 1);
    g_front = (g_front + 1) % RANDOM_DEGREE;
    g_rear = (g_rear + 1) % RANDOM_DEGREE;
    return result;
}

static void seed_locked(unsigned seed)
{
    int32_t word = seed == 0 ? 1 : (int32_t)seed;
    g_state[0] = word;
    for (int i = 1; i < RANDOM_DEGREE; i++) {
        int32_t high = word / PARK_MILLER_QUOTIENT;
        int32_t low = word % PARK_MILLER_QUOTIENT;
        word = PARK_MILLER_MULTIPLIER * low - PARK_MILLER_REMAINDER * high;
        if (word < 0) {
            word += RANDOM_MODULUS;
        }
        g_state[i] = word;
    }
    g_front = RANDOM_SEPARATION;
    g_rear = 0;
    for (int i = 0; i < RANDOM_DISCARD; i++) {
        next_random();
    }
    g_is_seeded = 1;
}

LIBC_FN void sysv_srandom(unsigned seed)
{
    AcquireSRWLockExclusive(&g_random_lock);
    seed_locked(seed);
    ReleaseSRWLockExclusive(&g_random_lock);
}

LIBC_FN c_long sysv_random(void)
{
    AcquireSRWLockExclusive(&g_random_lock);
    if (!g_is_seeded) {
        seed_locked(1);
    }
    c_long value = next_random();
    ReleaseSRWLockExclusive(&g_random_lock);
    return value;
}

LIBC_FN void sysv_srand(unsigned seed)
{
    sysv_srandom(seed);
}

LIBC_FN int sysv_rand(void)
{
    return (int)sysv_random();
}
