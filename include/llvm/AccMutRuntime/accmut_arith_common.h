#ifndef ACCMUT_ARITH_COMMON_H
#define ACCMUT_ARITH_COMMON_H

#include "stdint.h"

int32_t __accmut__cal_i32_arith(int32_t op, int32_t a, int32_t b);

int64_t __accmut__cal_i64_arith(int32_t op, int64_t a, int64_t b);

int32_t __accmut__cal_i32_bool(int32_t pred, int32_t a, int32_t b);

int32_t __accmut__cal_i64_bool(int32_t pred, int64_t a, int64_t b);


#endif
