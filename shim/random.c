/*
 * File: shim/random.c
 * Purpose: Cryptographic random numbers from the system generator: getrandom, getentropy and arc4random
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "libc_abi.h"

#include <bcrypt.h>

#define GETENTROPY_MAX 256

static int fill(void *buffer, size_t count)
{
    uint8_t *bytes = (uint8_t *)buffer;
    while (count > 0) {
        ULONG chunk = count > 0x10000000u ? 0x10000000u : (ULONG)count;
        if (BCryptGenRandom(NULL, bytes, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            return shim_fail(LINUX_EIO);
        }
        bytes += chunk;
        count -= chunk;
    }
    return 0;
}

LIBC_FN int64_t sysv_getrandom(void *buffer, size_t count, unsigned flags)
{
    (void)flags;
    return fill(buffer, count) == 0 ? (int64_t)count : -1;
}

LIBC_FN int sysv_getentropy(void *buffer, size_t count)
{
    if (count > GETENTROPY_MAX) {
        return shim_fail(LINUX_EIO);
    }
    return fill(buffer, count);
}

LIBC_FN uint32_t sysv_arc4random(void)
{
    uint32_t value = 0;
    fill(&value, sizeof(value));
    return value;
}

LIBC_FN void sysv_arc4random_buf(void *buffer, size_t count)
{
    fill(buffer, count);
}

/* A uniformly distributed value below `bound`, by rejecting the biased top of the range. */
LIBC_FN uint32_t sysv_arc4random_uniform(uint32_t bound)
{
    if (bound < 2) {
        return 0;
    }
    uint32_t minimum = (uint32_t)(-bound) % bound;
    uint32_t value;
    do {
        value = sysv_arc4random();
    } while (value < minimum);
    return value % bound;
}
