#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

typedef int32_t fp_14;

#define _f 0x4000
#define _fhalf 0x2000

#define _59div60 16110
#define _1div60 273

#define ntox(n) ((n) * _f)

#define xton_z(x) ((x) / _f)

#define xton_n(x) ((x) > 0 ? ((x) + _fhalf) / _f : ((x) - _fhalf) / _f )

#define x_add_y(x, y) ((x) + (y))

#define x_sub_y(x, y) ((x) - (y))

#define x_add_n(x, n) ((x) + n * _f)

#define x_sub_n(x, n) ((x) - n * _f)

#define x_mul_y(x, y) (((int64_t) (x)) * (y) / _f)

#define x_mul_n(x, n) ((x) * (n))

#define x_div_y(x, y) (((int64_t) (x)) * _f / (y))

#define x_div_n(x, n) ((x) / (n))

#endif
