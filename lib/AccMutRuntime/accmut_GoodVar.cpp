extern "C"
{
	#include "llvm/AccMutRuntime/accmut_GoodVar.h"
	#include "llvm/AccMutRuntime/accmut_arith_common.h"
	#include "llvm/AccMutRuntime/accmut_config.h"
    extern int32_t goodvar_fork(int mutID);
    extern int default_active_set[];
    extern int forked_active_set[];
    extern int forked_active_num;
}

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <vector>
#include <map>
#include <stack>
#include <algorithm>

using std::vector;
using std::map;

typedef int32_t GoodVarIdType;

const GoodVarIdType BADVAR = -1;

typedef int32_t OpCodeType;

template <class OpType, class RetType>
using CalcType = RetType(OpCodeType, OpType, OpType);

typedef int32_t MutationIdType;
typedef int64_t ValueType;
typedef map<MutationIdType, ValueType> GoodVarType;
typedef map<GoodVarIdType, GoodVarType> GoodVarTableType;

static std::stack<GoodVarTableType*> goodVarTableStack;
#define goodVarTable (*goodVarTableStack.top())

void __accmut__GoodVar_BBinit()
{
	goodVarTable.clear();
}


void __accmut__GoodVar_TablePush()
{
    goodVarTableStack.push(new GoodVarTableType());
}


void __accmut__GoodVar_TablePop()
{
    delete goodVarTableStack.top();
    goodVarTableStack.pop();
}


bool isActive(MutationIdType id)
{
    if(MUTATION_ID == 0)
    {
        return default_active_set[id] == 1;
    }
    else
    {
        int *forked_active_set_end = forked_active_set + forked_active_num;
        return std::find(forked_active_set, forked_active_set_end, id) != forked_active_set_end;
    }
}


static vector<MutationIdType> recent_set;
static vector<ValueType> temp_result;

template<class OpType, class RetType>
static void process_T_calc_GoodVar(OpCodeType op, OpType left, OpType right, CalcType<OpType, RetType> calc,
                                   MutationIdType leftId, MutationIdType rightId)
{
	GoodVarType &leftGoodVar = goodVarTable[leftId];
	GoodVarType &rightGoodVar = goodVarTable[rightId];

	// read good variable in id order

	auto leftIt = leftGoodVar.begin();
	auto rightIt = rightGoodVar.begin();

	for(;leftIt != leftGoodVar.end() && rightIt != rightGoodVar.end();)
	{
		if(leftIt->first < rightIt->first)
		{
		    if(isActive(leftIt->first))
            {
                recent_set.push_back(leftIt->first);
                temp_result.push_back((ValueType) calc(op, (OpType) leftIt->second, right));
            }
			++leftIt;
		}
		else if(leftIt->first > rightIt->first)
		{
            if(isActive(rightIt->first))
            {
                recent_set.push_back(rightIt->first);
                temp_result.push_back((ValueType) calc(op, left, (OpType) rightIt->second));
            }
			++rightIt;
		}
		else
		{
            if(isActive(leftIt->first))
            {
                recent_set.push_back(leftIt->first);
                temp_result.push_back((ValueType) calc(op, (OpType) leftIt->second, (OpType) rightIt->second));
            }
			++leftIt;
			++rightIt;
		}
	}

	for(;leftIt != leftGoodVar.end();)
	{
        if(isActive(leftIt->first))
        {
            recent_set.push_back(leftIt->first);
            temp_result.push_back((ValueType) calc(op, (OpType) leftIt->second, right));
        }
		++leftIt;
	}

	for(;rightIt != rightGoodVar.end();)
	{
        if(isActive(rightIt->first))
        {
            recent_set.push_back(rightIt->first);
            temp_result.push_back((ValueType) calc(op, left, (OpType) rightIt->second));
        }
		++rightIt;
	}
}


template<class OpType, class RetType>
static OpType process_T_calc_Mutation(OpCodeType op, OpType left, OpType right, CalcType<OpType, RetType> calc,
                                      MutationIdType mutId)
{
	if(mutId == 0)
	{
		return calc(op, left, right);
	}

	Mutation *m = ALLMUTS[mutId];

	switch(m->type)
    {
        case LVR:
            if (m->op_0 == 0)
            {
                return calc(op, (OpType)m->op_2, right);
            }
            else if (m->op_0 == 1)
            {
                return calc(op, left, (OpType)m->op_2);
            }
            else
            {
                assert(false);
            }

        case UOI:
        {
            if (m->op_1 == 0)
            {
                OpType u_left;
                if (m->op_2 == 0)
                {
                    u_left = left + 1;
                }
                else if (m->op_2 == 1)
                {
                    u_left = left - 1;
                }
                else if (m->op_2 == 2)
                {
                    u_left = -left;
                }
                else
                {
                    assert(false);
                }
                return calc(op, u_left, right);
            }
            else if (m->op_1 == 1)
            {
                long u_right;
                if (m->op_2 == 0)
                {
                    u_right = right + 1;
                }
                else if (m->op_2 == 1)
                {
                    u_right = right - 1;
                }
                else if (m->op_2 == 2)
                {
                    u_right = -right;
                }
                else
                {
                    assert(false);
                }
                return calc(op, left, u_right);
            }
            else
            {
                assert(false);
            }
        }

        case ROV:
            return calc(op, right, left);

        case ABV:
            if (m->op_0 == 0)
            {
                return calc(op, abs(left), right);
            }
            else if(m->op_0 == 1)
            {
                return calc(op, left, abs(right));
            }
            else
            {
                assert(false);
            }

        case AOR:
        case LOR:
        case COR:
        case SOR:
            return calc(m->op_0, left, right);

        case ROR:
            return calc((int32_t)m->op_2, left, right);

        default:
            assert(false);
    }
}


static void filter_variant(MutationIdType from, MutationIdType to)
{
    recent_set.clear();
    temp_result.clear();

    /*
    recent_set.push_back(0);

    if(MUTATION_ID == 0)
    {
        for (MutationIdType i = from; i < to; ++i)
        {
            recent_set.push_back(i);
        }
    }
     */

    if (MUTATION_ID == 0)
    {
        recent_set.push_back(0);
        for(MutationIdType i = from; i <= to; ++i)
        {
            if (default_active_set[i] == 1)
            {
                recent_set.push_back(i);
            }
        }
    }
    else
    {
        for(int i = 0; i < forked_active_num; ++i)
        {
            if (forked_active_set[i] >= from && forked_active_set[i] <= to)
            {
                recent_set.push_back(forked_active_set[i]);
            }
        }
        if(recent_set.empty())
        {
        	recent_set.push_back(0);
        }
    }

}


static ValueType fork_eqclass()
{
    map<ValueType, vector<MutationIdType>> eqclasses;
    vector<MutationIdType> mainClass;

    for(vector<MutationIdType>::size_type i = 1; i != recent_set.size(); ++i)
    {
        if(temp_result[i] != temp_result[0])
        {
            eqclasses[temp_result[i]].push_back(recent_set[i]);
        }
        else if(MUTATION_ID != 0)
        {
            mainClass.push_back(recent_set[i]);
        }
    }

    for(auto it = eqclasses.begin(); it != eqclasses.end(); ++it)
    {
        int32_t isChild = goodvar_fork(it->second.front());

        if(isChild)
        {
            forked_active_num = 0;
            for(MutationIdType id : it->second)
            {
                forked_active_set[forked_active_num] = id;
                ++forked_active_num;
            }
            return it->first;
        }
        else if(MUTATION_ID == 0)
        {
            for(MutationIdType id : it->second)
            {
                default_active_set[id] = 0;
            }
        }
    }

    if(MUTATION_ID != 0)
    {
        forked_active_num = 0;
        for(MutationIdType id : mainClass)
        {
            forked_active_set[forked_active_num] = id;
            ++forked_active_num;
        }
    }

    return temp_result[0];
}


template <class OpType, class RetType>
static RetType process_T_arith_GoodVar
        (MutationIdType from, MutationIdType to, OpType left, OpType right,
         GoodVarIdType retId, GoodVarIdType leftId, GoodVarIdType rightId, OpCodeType op,
         CalcType<OpType, RetType> calc)
{
    // TODO: We only consider order1 mutations, so mutations in following two parts won't intersect

    // calc mutation results
    filter_variant(from, to);
    for (MutationIdType mutId : recent_set)
    {
        temp_result.push_back((ValueType)process_T_calc_Mutation(op, left, right, calc, mutId));
    }

    // read good variables and calc results
    if(!goodVarTable.empty())
    {
        process_T_calc_GoodVar(op, left, right, calc, leftId, rightId);
    }

    if(retId == BADVAR)
    {
        return (RetType)fork_eqclass();
    }
    else
    {
        GoodVarType &retGoodVar = goodVarTable[retId];

        // TODO: Here only consider order1 mutation, skip the first mutation which is actually running on CPU
        for (vector<MutationIdType>::size_type i = 1; i != recent_set.size(); ++i)
        {
            if (temp_result[i] != temp_result[0])
            {
                retGoodVar[recent_set[i]] = temp_result[i];
            }
        }

        return (RetType)temp_result[0];
    }
}


int32_t __accmut__process_i32_arith_GoodVar
	(int32_t from, int32_t to, int32_t left, int32_t right,
	 int32_t retId, int32_t leftId, int32_t rightId, int32_t op)
{
	return process_T_arith_GoodVar(from, to, left, right, retId, leftId, rightId, op, __accmut__cal_i32_arith);
}


int64_t __accmut__process_i64_arith_GoodVar
	(int32_t from, int32_t to, int64_t left, int64_t right,
	 int32_t retId, int32_t leftId, int32_t rightId, int32_t op)
{
	return process_T_arith_GoodVar(from, to, left, right, retId, leftId, rightId, op, __accmut__cal_i64_arith);
}


int32_t __accmut__process_i32_cmp_GoodVar
	(int32_t from, int32_t to, int32_t left, int32_t right,
	 int32_t retId, int32_t leftId, int32_t rightId, int32_t pred)
{
    assert(retId == BADVAR);
    return process_T_arith_GoodVar(from, to, left, right, retId, leftId, rightId, pred, __accmut__cal_i32_bool);
}


int32_t __accmut__process_i64_cmp_GoodVar
	(int32_t from, int32_t to, int64_t left, int64_t right,
	 int32_t retId, int32_t leftId, int32_t rightId, int32_t pred)
{
    assert(retId == BADVAR);
	return process_T_arith_GoodVar(from, to, left, right, retId, leftId, rightId, pred, __accmut__cal_i64_bool);
}