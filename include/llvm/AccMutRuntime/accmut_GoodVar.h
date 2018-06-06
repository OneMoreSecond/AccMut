#ifndef ACCMUT_GOODVAR_H
#define ACCMUT_GOODVAR_H

#include "stdint.h"

void __accmut__GoodVar_BBinit();

void __accmut__GoodVar_TablePush();

void __accmut__GoodVar_TablePop();

int32_t __accmut__process_i32_arith_GoodVar
		(int32_t from, int32_t to, int32_t left, int32_t right,
		 int32_t retId, int32_t leftId, int32_t rightId, int32_t op);

int64_t __accmut__process_i64_arith_GoodVar
		(int32_t from, int32_t to, int64_t left, int64_t right,
		 int32_t retId, int32_t leftId, int32_t rightId, int32_t op);

int32_t __accmut__process_i32_cmp_GoodVar
		(int32_t from, int32_t to, int32_t left, int32_t right,
		 int32_t retId, int32_t leftId, int32_t rightId, int32_t pred);

int32_t __accmut__process_i64_cmp_GoodVar
		(int32_t from, int32_t to, int64_t left, int64_t right,
		 int32_t retId, int32_t leftId, int32_t rightId, int32_t pred);

#endif