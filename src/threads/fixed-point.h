#ifndef _THREAD_FIXED_POINT_H
#define _THREAD_FIXED_POINT_H

#include <stdint.h>

typedef int32_t fixed_point_t;

#define ___f (0x4000)
// 1 << 14

#define to_fp(n) ((n)*___f)

// convert x to integer rounding toward zero
#define to_intz(x) ((x) / ___f)

// convert x to integer rounding toward nearest
#define to_intn(x) ((((x) >= 0            \
                        ? (x) + (___f / 2) \
                        : (x) - (___f / 2)))\
                        / ___f)

#define fp_add(x, y) ((x) + (y))

#define fp_sub(x, y) ((x) - (y))

#define fp_int_add(x, n) (fp_add(x, to_fp(n)))

#define fp_int_sub(x, n) (fp_sub(x, to_fp(n)))

#define fp_mul(x, y) (((int64_t)(x)) * (y) / ___f)

#define fp_int_mul(x, n) ((x) * (n))

#define fp_div(x, y) (((int64_t)(x)) * ___f / (y))

#define fp_int_div(x, n) ((x) / (n))

#endif // _THREAD_FIXED_POINT_H
