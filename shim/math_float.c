/*
 * File: shim/math_float.c
 * Purpose: Single-precision libm (sinf, powf, ...), rounding to integers, sincos and the frexp/ldexp family
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <math.h>

#include "libc_abi.h"

#define MATHF1(name) LIBC_FN float sysv_##name(float x) { return name(x); }
#define MATHF2(name) LIBC_FN float sysv_##name(float x, float y) { return name(x, y); }

MATHF1(sqrtf) MATHF1(cbrtf) MATHF1(fabsf) MATHF1(floorf) MATHF1(ceilf) MATHF1(truncf) MATHF1(roundf)
MATHF1(rintf) MATHF1(nearbyintf) MATHF1(sinf) MATHF1(cosf) MATHF1(tanf) MATHF1(asinf) MATHF1(acosf)
MATHF1(atanf) MATHF1(sinhf) MATHF1(coshf) MATHF1(tanhf) MATHF1(asinhf) MATHF1(acoshf) MATHF1(atanhf)
MATHF1(expf) MATHF1(exp2f) MATHF1(expm1f) MATHF1(logf) MATHF1(log2f) MATHF1(log10f) MATHF1(log1pf)
MATHF1(logbf) MATHF1(erff) MATHF1(erfcf) MATHF1(tgammaf)
MATHF2(powf) MATHF2(fmodf) MATHF2(atan2f) MATHF2(hypotf) MATHF2(fminf) MATHF2(fmaxf) MATHF2(fdimf)
MATHF2(remainderf) MATHF2(copysignf) MATHF2(nextafterf)

/* long is 64 bits for translated code; the CRT's lroundf returns 32 bits, so the 64-bit forms are used. */
LIBC_FN int64_t sysv_lroundf(float x)
{
    return llroundf(x);
}

LIBC_FN int64_t sysv_llroundf(float x)
{
    return llroundf(x);
}

LIBC_FN int64_t sysv_lrintf(float x)
{
    return llrintf(x);
}

LIBC_FN int64_t sysv_llrintf(float x)
{
    return llrintf(x);
}

LIBC_FN void sysv_sincos(double x, double *sine, double *cosine)
{
    *sine = sin(x);
    *cosine = cos(x);
}

LIBC_FN void sysv_sincosf(float x, float *sine, float *cosine)
{
    *sine = sinf(x);
    *cosine = cosf(x);
}

LIBC_FN float sysv_frexpf(float x, int *exponent)
{
    return frexpf(x, exponent);
}

LIBC_FN float sysv_ldexpf(float x, int exponent)
{
    return ldexpf(x, exponent);
}

LIBC_FN float sysv_modff(float x, float *whole)
{
    return modff(x, whole);
}

LIBC_FN float sysv_scalbnf(float x, int exponent)
{
    return scalbnf(x, exponent);
}

LIBC_FN int sysv_ilogbf(float x)
{
    return ilogbf(x);
}

LIBC_FN float sysv_fmaf(float x, float y, float z)
{
    return fmaf(x, y, z);
}

/* glibc's double classification function, used by the isnan/isinf/fpclassify macros. */
enum { FP_NAN_CLASS = 0, FP_INFINITE_CLASS = 1, FP_ZERO_CLASS = 2, FP_SUBNORMAL_CLASS = 3, FP_NORMAL_CLASS = 4 };

LIBC_FN int sysv___fpclassify(double x)
{
    switch (fpclassify(x)) {
    case FP_NAN:
        return FP_NAN_CLASS;
    case FP_INFINITE:
        return FP_INFINITE_CLASS;
    case FP_ZERO:
        return FP_ZERO_CLASS;
    case FP_SUBNORMAL:
        return FP_SUBNORMAL_CLASS;
    default:
        return FP_NORMAL_CLASS;
    }
}
