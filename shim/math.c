/*
 * File: shim/math.c
 * Purpose: libm for translated programs, delegating to the C runtime's correctly behaving routines
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Results can differ from glibc in the last bit for some functions. tests/corpus compares printed
 * output against real glibc runs, which is how any such difference is found and then fixed here.
 */

#include <limits.h>
#include <math.h>

#include "libc_abi.h"

#define MATH1(name) LIBC_FN double sysv_##name(double x) { return name(x); }
#define MATH2(name) LIBC_FN double sysv_##name(double x, double y) { return name(x, y); }

MATH1(sqrt) MATH1(cbrt) MATH1(fabs) MATH1(floor) MATH1(ceil) MATH1(trunc) MATH1(round) MATH1(rint)
MATH1(nearbyint) MATH1(sin) MATH1(cos) MATH1(tan) MATH1(asin) MATH1(acos) MATH1(atan) MATH1(sinh)
MATH1(cosh) MATH1(tanh) MATH1(asinh) MATH1(acosh) MATH1(atanh) MATH1(exp) MATH1(exp2) MATH1(expm1)
MATH1(log) MATH1(log2) MATH1(log10) MATH1(log1p) MATH1(logb) MATH1(erf) MATH1(erfc) MATH1(tgamma)
MATH2(pow) MATH2(fmod) MATH2(atan2) MATH2(hypot) MATH2(fmin) MATH2(fmax) MATH2(fdim) MATH2(remainder)
MATH2(copysign) MATH2(nextafter)

LIBC_FN double sysv_fma(double x, double y, double z)
{
    return fma(x, y, z);
}

LIBC_FN double sysv_ldexp(double x, int exponent)
{
    return ldexp(x, exponent);
}

LIBC_FN double sysv_scalbln(double x, c_long exponent)
{
    return scalbn(x, exponent > INT_MAX ? INT_MAX : exponent < INT_MIN ? INT_MIN : (int)exponent);
}

LIBC_FN double sysv_frexp(double x, int *exponent)
{
    return frexp(x, exponent);
}

LIBC_FN double sysv_modf(double x, double *integral)
{
    return modf(x, integral);
}

LIBC_FN double sysv_nexttoward(double x, long double toward)
{
    return nexttoward(x, toward);
}

LIBC_FN c_long sysv_lround(double x)
{
    return (c_long)llround(x);
}

LIBC_FN long long sysv_llround(double x)
{
    return llround(x);
}

LIBC_FN c_long sysv_lrint(double x)
{
    return (c_long)llrint(x);
}

LIBC_FN double sysv_drem(double x, double y)
{
    return remainder(x, y);
}

LIBC_FN double sysv_exp10(double x)
{
    return pow(10.0, x);
}

LIBC_FN double sysv_scalb(double x, double exponent)
{
    return exponent == floor(exponent) ? scalbn(x, (int)exponent) : NAN;
}

LIBC_FN double sysv_significand(double x)
{
    return (x == 0 || isinf(x) || isnan(x)) ? x : scalbn(x, -ilogb(x));
}

/* The sign of gamma(x): negative between a negative even integer and the next negative odd one. */
LIBC_FN double sysv_lgamma_r(double x, int *sign)
{
    int is_negative = x < 0 && floor(x) != x && fmod(floor(x), 2.0) != 0;
    *sign = is_negative ? -1 : 1;
    return lgamma(x);
}

LIBC_FN double sysv_lgamma(double x)
{
    int ignored;
    return sysv_lgamma_r(x, &ignored);
}

LIBC_FN double sysv_gamma(double x)
{
    return sysv_lgamma(x);
}

LIBC_FN double sysv_j0(double x)
{
    return _j0(x);
}

LIBC_FN double sysv_j1(double x)
{
    return _j1(x);
}

LIBC_FN double sysv_jn(int order, double x)
{
    return _jn(order, x);
}

LIBC_FN double sysv_y0(double x)
{
    return _y0(x);
}

LIBC_FN double sysv_y1(double x)
{
    return _y1(x);
}

LIBC_FN double sysv_yn(int order, double x)
{
    return _yn(order, x);
}
