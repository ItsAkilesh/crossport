/*
 * File: shim/libc_extra.c
 * Purpose: rand_r, long-double math (expl, logl, modfl, roundl, sqrtl), fgetpos and fsetpos, floating-point environment queries, the thread
 *          cancellation hooks and gethostbyname2_r: the entries the third application sweep found missing
 * Author: Akilesh
 * Date: 2026-11-24
 */

#include <math.h>
#include <string.h>

#include "libc_abi.h"
#include "stdio_internal.h"

#define LCG_MULTIPLIER 1103515245u
#define LCG_INCREMENT 12345u
#define HIGH_BITS_DIVISOR 65536u
#define FIRST_DRAW_MODULUS 2048u
#define LATER_DRAW_MODULUS 1024u
#define DRAW_SHIFT 10
#define POSITION_BYTES 16

LIBC_FN int64_t sysv_ftello(ShimFile *file);
LIBC_FN int sysv_clock_gettime(int clock_id, int64_t *ts);
LIBC_FN int sysv_fseeko(ShimFile *file, int64_t offset, int whence);

/* glibc's rand_r: three draws of the linear congruential generator are combined, so a given seed gives the same numbers on Linux. */
LIBC_FN int sysv_rand_r(unsigned *seed)
{
    unsigned next = *seed;
    next = next * LCG_MULTIPLIER + LCG_INCREMENT;
    int result = (int)((next / HIGH_BITS_DIVISOR) % FIRST_DRAW_MODULUS);
    next = next * LCG_MULTIPLIER + LCG_INCREMENT;
    result = (int)((unsigned)result << DRAW_SHIFT) ^ (int)((next / HIGH_BITS_DIVISOR) % LATER_DRAW_MODULUS);
    next = next * LCG_MULTIPLIER + LCG_INCREMENT;
    result = (int)((unsigned)result << DRAW_SHIFT) ^ (int)((next / HIGH_BITS_DIVISOR) % LATER_DRAW_MODULUS);
    *seed = next;
    return result;
}

LIBC_FN long double sysv_expl(long double value)
{
    return expl(value);
}

LIBC_FN long double sysv_logl(long double value)
{
    return logl(value);
}

LIBC_FN long double sysv_sqrtl(long double value)
{
    return sqrtl(value);
}

LIBC_FN long double sysv_roundl(long double value)
{
    return roundl(value);
}

LIBC_FN long double sysv_modfl(long double value, long double *integral)
{
    return modfl(value, integral);
}

/* fpos_t holds the offset in its first eight bytes; the conversion state that follows is unused (the locale is UTF-8 and stateless). */
LIBC_FN int sysv_fgetpos(ShimFile *file, uint8_t *position)
{
    int64_t offset = sysv_ftello(file);
    if (offset < 0) {
        return -1;
    }
    memset(position, 0, POSITION_BYTES);
    memcpy(position, &offset, sizeof(offset));
    return 0;
}

LIBC_FN int sysv_fgetpos64(ShimFile *file, uint8_t *position)
{
    return sysv_fgetpos(file, position);
}

LIBC_FN int sysv_fsetpos(ShimFile *file, const uint8_t *position)
{
    int64_t offset;
    memcpy(&offset, position, sizeof(offset));
    return sysv_fseeko(file, offset, LINUX_SEEK_SET);
}

LIBC_FN int sysv_fsetpos64(ShimFile *file, const uint8_t *position)
{
    return sysv_fsetpos(file, position);
}

/* No floating-point exception flag is ever raised that the shim could report, and clearing them is always allowed. */
LIBC_FN int sysv_feclearexcept(int excepts)
{
    (void)excepts;
    return 0;
}

LIBC_FN int sysv_fetestexcept(int excepts)
{
    (void)excepts;
    return 0;
}

LIBC_FN int sysv_feraiseexcept(int excepts)
{
    (void)excepts;
    return 0;
}

/* pthread_cleanup_push and _pop expand to these when a thread is cancelled; nothing here cancels threads, so there is nothing to register. */
LIBC_FN void sysv___pthread_register_cancel(void *buffer)
{
    (void)buffer;
}

LIBC_FN void sysv___pthread_unregister_cancel(void *buffer)
{
    (void)buffer;
}

/* The hosts database is empty. */
LIBC_FN int sysv_gethostbyname2_r(const char *name, int family, void *result_buffer, char *buffer, size_t size, void **result, int *error)
{
    (void)name;
    (void)family;
    (void)result_buffer;
    (void)buffer;
    (void)size;
    *result = NULL;
    *error = 1; /* HOST_NOT_FOUND */
    return LINUX_ENOENT;
}

/* timespec_get(3) (C11): TIME_UTC (1) is the wall clock. Returns the base on success and 0 for any other base. */
LIBC_FN int sysv_timespec_get(int64_t *time, int base)
{
    enum { TIME_UTC_BASE = 1, CLOCK_REALTIME_ID = 0 };
    return base == TIME_UTC_BASE && sysv_clock_gettime(CLOCK_REALTIME_ID, time) == 0 ? base : 0;
}
