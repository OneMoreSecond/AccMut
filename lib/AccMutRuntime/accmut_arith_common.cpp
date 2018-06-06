extern "C"
{
#include "llvm/AccMutRuntime/accmut_arith_common.h"
}

#include <cstdio>
#include <cassert>

// can't include them due to link problem

/*
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"

using namespace llvm;
*/

// manually copy enum definitions from llvm 6.0

enum Instruction
{
    Add = 11,
    Sub = 13,
    Mul = 15,
    UDiv = 17,
    SDiv = 18,
    URem = 20,
    SRem = 21,
    Shl = 23,
    LShr = 24,
    AShr = 25,
    And = 26,
    Or = 27,
    Xor = 28,
};

enum CmpInst
{
    ICMP_EQ    = 32,  ///< equal
    ICMP_NE    = 33,  ///< not equal
    ICMP_UGT   = 34,  ///< unsigned greater than
    ICMP_UGE   = 35,  ///< unsigned greater or equal
    ICMP_ULT   = 36,  ///< unsigned less than
    ICMP_ULE   = 37,  ///< unsigned less or equal
    ICMP_SGT   = 38,  ///< signed greater than
    ICMP_SGE   = 39,  ///< signed greater or equal
    ICMP_SLT   = 40,  ///< signed less than
    ICMP_SLE   = 41,  ///< signed less or equal
};

template <class T, class UT>
T cal_T_arith(int32_t op, T a, T b, T undefValue)
{
    // TODO:: add float point
    switch(op)
    {
        case Instruction::Add :
            return a + b;

        case Instruction::Sub :
            return a - b;

        case Instruction::Mul :
            return a * b;

        case Instruction::UDiv :
            return b == 0 ? undefValue : (T)((UT)a / (UT)b);

        case Instruction::SDiv :
            return b == 0 ? undefValue : a / b;

        case Instruction::URem :
            return b == 0 ? undefValue : (T)((UT)a % (UT)b);

        case Instruction::SRem :
            return b == 0 ? undefValue : a % b;

        case Instruction::Shl :
            return a << b;

        case Instruction::LShr :
            return (T)((UT)a >> b);

        case Instruction::AShr :
            return a >> b;

        case Instruction::And :
            return a & b;

        case Instruction::Or :
            return a | b;

        case Instruction::Xor :
            return a ^ b;

        default:
            fprintf(stderr, "OpCode: %d\n", op);
            assert(false);
    }
}


int32_t __accmut__cal_i32_arith(int32_t op, int32_t a, int32_t b)
{
    return cal_T_arith<int32_t, uint32_t>(op, a, b, INT32_MAX);
}


int64_t __accmut__cal_i64_arith(int32_t op, int64_t a, int64_t b)
{
    return cal_T_arith<int64_t, uint64_t>(op, a, b, INT64_MAX);
}


template <class T, class UT>
T cal_T_bool(int32_t pred, T a, T b)
{
    switch(pred)
    {
        case CmpInst::ICMP_EQ :
            return a == b;

        case CmpInst::ICMP_NE:
            return a != b;

        case CmpInst::ICMP_UGT:
            return (UT)a > (UT)b;

        case CmpInst::ICMP_UGE :
            return (UT)a >= (UT)b;

        case CmpInst::ICMP_ULT :
            return (UT)a < (UT)b;

        case CmpInst::ICMP_ULE :
            return (UT)a <= (UT)b;

        case CmpInst::ICMP_SGT :
            return a > b;

        case CmpInst::ICMP_SGE :
            return a >= b;

        case CmpInst::ICMP_SLT :
            return a < b;

        case CmpInst::ICMP_SLE :
            return a <= b;

        default:
            fprintf(stderr, "PredCode: %d\n", pred);
            assert(false);
    }
}


int32_t __accmut__cal_i32_bool(int32_t pred, int32_t a, int32_t b)
{
	return cal_T_bool<int32_t, uint32_t>(pred, a, b);
}


int32_t __accmut__cal_i64_bool(int32_t pred, int64_t a, int64_t b)
{
    return cal_T_bool<int64_t, uint64_t>(pred, a, b);
}

