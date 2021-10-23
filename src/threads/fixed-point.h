#ifndef _THREAD_FIXED_POINT_H
#define _THREAD_FIXED_POINT_H

#include <stdint.h>

#define q 14
#define f (1 << q)

#define to_fp (n)((n)*f)

// convert x to integer rounding toward zero
#define to_intz (x)((x) / f)

// convert x to integer rounding toward nearest
#define to_intn (x)((x) >= 0            \
                        ? (x) + (f / 2) \
                        : (x) - (f / 2))

#define fp_add (x, y)((x) + (y))

#define fp_sub (x, y)((x) - (y))

#define fp_int_add (x, n)(fp_add(x, to_fp(n)))

#define fp_int_sub (x, n)(fp_sub(x, to_fp(n)))

#define fp_mul (x, y)(((int64_t)(x)) * to_intz(y))

#define fp_int_mul (x, n)((x) * (n))

#define fp_div (x, y)(((int64_t)(x)) * f / (y))

#define fp_int_div (x, n)((x) / (n))

#endif // _THREAD_FIXED_POINT_H
