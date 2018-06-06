//
// Created by txh on 18-4-2.
//
// This file describes IR instrument pass of the dynamic mutation analysis with window mechanism.
//

#ifndef LLVM_WAINSTRUMENT_H
#define LLVM_WAINSTRUMENT_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"

#include "llvm/Transforms/AccMut/Mutation.h"

using namespace llvm;

#include <vector>
#include <map>
#include <set>
#include <string>

using std::vector;
using std::map;
using std::set;
using std::string;

class WAInstrumenter : public ModulePass
{
public:

    static char ID;// Pass identification, replacement for typeid

    WAInstrumenter();

    bool runOnModule(Module &M);

private:

    Module *TheModule;

    bool runOnFunction(Function &F);

    Function *goodVarTableInitFunc;
    Function *goodVarTablePushFunc;
    Function *goodVarTablePopFunc;
    Function *goodVarI32ArithFunc;
    Function *goodVarI64ArithFunc;
    Function *goodVarI32CmpFunc;
    Function *goodVarI64CmpFunc;
    void setGoodVarFunc();
    void setGoodVarVoidFunc(Function *&F, string name, Type *voidType);
    void setGoodVarBinaryFunc(Function *&F, string name, Type *retType, Type *paramType);

    map<Instruction*, vector<Mutation*>> instMutsMap;
    void getInstMutsMap(vector<Mutation*> *v, Function &F);

    map<Instruction*, int> goodVariables;
    void getGoodVariables(BasicBlock &BB);
    void filterRealGoodVariables();
    bool hasUsedPreviousGoodVariables(Instruction *I);
    void pushGoodVarIdInfo(vector<Value*> &params, Instruction *I);
    ConstantInt* getGoodVarId(Value *I);
    ConstantInt* getI32Constant(int value);

    bool instrument(BasicBlock &BB);
    void instrumentAboutGoodVariable(Instruction &I);
    void instrumentAsDMA(Instruction &I, bool aboutGoodVariable = false);

    static bool isSupportedOpcode(Instruction *I);
    static bool isSupportedOp(Instruction *I);
    static bool isSupportedInstruction(Instruction *I);
    static bool isSupportedBoolInstruction(Instruction *I);
    static bool isSupportedType(Value *V);
};

#endif //LLVM_WAINSTRUMENT_H
