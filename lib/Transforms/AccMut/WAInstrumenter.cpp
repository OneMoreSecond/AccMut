//
// Created by txh on 18-4-2.
//

#include <sstream>
#include "llvm/Transforms/AccMut/WAInstrumenter.h"
#include "llvm/Transforms/AccMut/MutUtil.h"

#include "llvm/IR/InstIterator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;


WAInstrumenter::WAInstrumenter() : ModulePass(ID) {}


bool WAInstrumenter::runOnModule(Module &M)
{
    MutUtil::getAllMutations();

    TheModule = &M;
    setGoodVarFunc();

    bool changed = false;

    for(Function &F : M)
        if(!F.isDeclaration())
        {
            changed |= runOnFunction(F);
        }

    return changed;
}


bool WAInstrumenter::runOnFunction(Function &F)
{
    if(F.getName().startswith("__accmut__"))
    {
        return false;
    }

    if(F.getName().equals("main"))
    {
        // We will change it manually
        return true;
    }

    vector<Mutation*> *v= MutUtil::AllMutsMap[F.getName()];

    if(v == NULL || v->size() == 0)
    {
        return false;
    }

    errs() << "\n######## WA INSTRUMTNTING MUT @ " << F.getName() << "()  ########\n\n";

    getInstMutsMap(v, F);

    bool aboutGoodVariables = false;

    for(auto it = F.begin(), end = F.end(); it != end;)
    {
        // avoid iterator invalidation
        BasicBlock &BB = *it;
        ++it;

        getGoodVariables(BB);
        /*
        errs() << "---good variables---\n";
        for(auto it = goodVariables.begin(); it != goodVariables.end(); ++it)
        {
            errs() << *it->first << "\n";
        }
        */
        filterRealGoodVariables();

        /*errs() << "---after filter---\n";
        for(auto it = goodVariables.begin(); it != goodVariables.end(); ++it)
        {
            errs() << *it->first << "\n";
        }*/

        aboutGoodVariables |= instrument(BB);
    }

    if(aboutGoodVariables)
    {
        CallInst::Create(goodVarTablePushFunc, "", F.begin()->getFirstNonPHI());
        for(auto it = inst_begin(F), end = inst_end(F); it != end; ++it)
        {
            if(it->getOpcode() == Instruction::Ret)
            {
                CallInst::Create(goodVarTablePopFunc, "", &*it);
            }
        }
    }

    return aboutGoodVariables;
}


void WAInstrumenter::setGoodVarFunc()
{
    LLVMContext &C = TheModule->getContext();

    Type *voidTy = Type::getVoidTy(C);
    setGoodVarVoidFunc(goodVarTableInitFunc, "__accmut__GoodVar_BBinit", voidTy);
    setGoodVarVoidFunc(goodVarTablePushFunc, "__accmut__GoodVar_TablePush", voidTy);
    setGoodVarVoidFunc(goodVarTablePopFunc, "__accmut__GoodVar_TablePop", voidTy);

    Type *i32 = Type::getInt32Ty(C);
    Type *i64 = Type::getInt64Ty(C);
    setGoodVarBinaryFunc(goodVarI32ArithFunc, "__accmut__process_i32_arith_GoodVar", i32, i32);
    setGoodVarBinaryFunc(goodVarI64ArithFunc, "__accmut__process_i64_arith_GoodVar", i64, i64);
    setGoodVarBinaryFunc(goodVarI32CmpFunc, "__accmut__process_i32_cmp_GoodVar", i32, i32);
    setGoodVarBinaryFunc(goodVarI64CmpFunc, "__accmut__process_i64_cmp_GoodVar", i32, i64);
}


void WAInstrumenter::setGoodVarVoidFunc(Function *&F, string name, Type *voidType)
{
    F = TheModule->getFunction(name);

    if (F == nullptr)
    {
        F = Function::Create(
                /*Type=*/ FunctionType::get(voidType, false),
                /*Linkage=*/ GlobalValue::ExternalLinkage,
                /*Name=*/ name,
                          TheModule); // (external, no body)
    }
}


void WAInstrumenter::setGoodVarBinaryFunc(Function *&F, string name, Type *retType, Type *paramType)
{
    F = TheModule->getFunction(name);

    if (F == nullptr)
    {
        Type *i32 = Type::getInt32Ty(TheModule->getContext());

        // mut_from mut_to operand1 operand2 goodVarIdRet goodVarIdOp1 goodVarOp2 originalOp
        vector<Type*> paramTypes{i32, i32, paramType, paramType, i32, i32, i32, i32};

        F = Function::Create(
                /*Type=*/ FunctionType::get(retType, paramTypes, false),
                /*Linkage=*/ GlobalValue::ExternalLinkage,
                /*Name=*/ name,
                          TheModule); // (external, no body)
    }
}


bool WAInstrumenter::instrument(BasicBlock &BB)
{
    bool changed = false;
    bool aboutGoodVariables = false;
/*
    for(auto it = BB.rbegin(), end = BB.rend(); it != end; ++it)
    {
        errs() << it.getNodePtr() << "---" << it->getPrevNode() << "\n";
    }
    errs() << "*****" << BB.rend().getNodePtr() << "\n";
*/
    //errs() << "One basic block begin\n";
    //auto &first = *BB.begin();

    // avoid hasUsedPreviousGoodVariables invalidation by iterating reversely
    for(auto it = BB.rbegin(), end = BB.rend(); it != end;)
    {
        //errs() << it.getNodePtr() << "---" << it->getPrevNode() << "\n";
        //errs() << "\n---F---" << *BB.getParent() << "---F---\n";

        // avoid iterator invalidation
        Instruction &I = *it;
        ++it;

        if(goodVariables.count(&I) == 1 || hasUsedPreviousGoodVariables(&I))
        {
            instrumentAboutGoodVariable(I);
            changed = true;
            aboutGoodVariables = true;
        }
        else if(instMutsMap.count(&I) == 1)
        {
            instrumentAsDMA(I);
            changed = true;
        }
    }

    //errs() << "One basic block end\n";

    // Insert a good variable table initialization call before the first non-PHI instruction
    if(aboutGoodVariables)
    {
        CallInst::Create(goodVarTableInitFunc, "", BB.getFirstNonPHI());
        //errs() << "Init func added\n";
    }

    return aboutGoodVariables;
}


void WAInstrumenter::instrumentAboutGoodVariable(Instruction &I)
{
    instrumentAsDMA(I, true);
}


// Following code mostly comes from Wang Bo's project

#define VALERRMSG(it,msg,cp) llvm::errs()<<"\tCUR_IT:\t"<<(*(it))<<"\n\t"<<(msg)<<":\t"<<(*(cp))<<"\n"

#define ERRMSG(msg) llvm::errs()<<(msg)<<" @ "<<__FILE__<<"->"<<__FUNCTION__<<"():"<<__LINE__<<"\n"

//TYPE BITS OF SIGNATURE
#define CHAR_TP 0
#define SHORT_TP 1
#define INT_TP 2
#define LONG_TP 3

static int getTypeMacro(Type *t){
    int res = -1;
    if(t->isIntegerTy()){
        unsigned width = t->getIntegerBitWidth();
        switch(width){
            case 8:
                res = CHAR_TP;
                break;
            case 16:
                res = SHORT_TP;
                break;
            case 32:
                res = INT_TP;
                break;
            case 64:
                res = LONG_TP;
                break;
            default:
                ERRMSG("OMMITNG PARAM TYPE");
                llvm::errs()<<*t<<'\n';
                //exit(0);
        }
    }
    return res;
}

static bool isHandledCoveInst(Instruction *inst){
    return inst->getOpcode() == Instruction::Trunc
           || inst->getOpcode() == Instruction::ZExt
           || inst->getOpcode() == Instruction::SExt
           || inst->getOpcode() == Instruction::BitCast ;
    // TODO:: PtrToInt, IntToPtr, AddrSpaceCast and float related Inst are not handled
}

static bool pushPreparecallParam(std::vector<Value*>& params, int index, Value *OI, Module *TheModule){
    Type* OIType = (dyn_cast<Value>(&*OI))->getType();
    int tp = getTypeMacro(OIType);
    if(tp < 0){
        return false;
    }

    std::stringstream ss;
    //push type
    ss.str("");
    unsigned short tp_and_idx = ((unsigned short)tp)<<8;
    tp_and_idx = tp_and_idx | index;
    ss<<tp_and_idx;
    ConstantInt* c_t_a_i = ConstantInt::get(TheModule->getContext(),
                                            APInt(16, StringRef(ss.str()), 10));

    //now push the pointer of idx'th param
    if(LoadInst *ld = dyn_cast<LoadInst>(&*OI)){//is a local var
        Value *ptr_of_ld = ld->getPointerOperand();
        //if the pointer of loadInst dose not point to an integer
        if(SequentialType *t = dyn_cast<SequentialType>(ptr_of_ld->getType())){
            if(! t->getElementType()->isIntegerTy()){// TODO: for i32**
                ERRMSG("WARNNING ! Trying to push a none-i32* !! ");
                return false;
            }
        }
        params.push_back(c_t_a_i);
        params.push_back(ptr_of_ld);
    }/*else if(AllocaInst *alloca = dyn_cast<AllocaInst>(&*OI)){// a param of the F, fetch it by alloca
        params.push_back(c_t_a_i);
        params.push_back(alloca);
    }else if(GetElementPtrInst *ge = dyn_cast<GetElementPtrInst>(&*OI)){
        // TODO: test
        params.push_back(c_t_a_i);
        params.push_back(ge);
    }*/
        // TODO:: for Global Pointer ?!
    else{
        ERRMSG("CAN NOT GET A POINTER");
        Value *v = dyn_cast<Value>(&*OI);
        llvm::errs()<<"\tCUR_OPREAND:\t";
        v->dump();
        exit(0);
    }
    return true;
}

void WAInstrumenter::instrumentAsDMA(Instruction &I, bool aboutGoodVariable)
{
    // TODO: test without it first

    int instrumented_insts = 0;

    Instruction *cur_it = &I;
    //cur_it =  getLocation(F, instrumented_insts, tmp[0]->index);

    vector<Mutation*> &tmp = instMutsMap[cur_it];
    int mut_from, mut_to;

    if(tmp.empty())
    {
        mut_from = -1;
        mut_to = -2;
    }
    else
    {
        mut_from = tmp.front()->id;
        mut_to = tmp.back()->id;
    }

    llvm::errs() << "CUR_INST: " << tmp.front()->index
                 << "  (FROM:" << mut_from << "  TO:" << mut_to << "  AboutGoodVar: " << aboutGoodVariable << ")\t"
                 << *cur_it << "\n";

    if(tmp.size() >= MAX_MUT_NUM_PER_LOCATION){
        ERRMSG("TOO MANY MUTS ");
        exit(0);
    }


    // CallInst Navigation
    if(dyn_cast<CallInst>(&*cur_it)){
        //move all constant literal and SSA value to repalce to alloca, e.g foo(a+5)->b = a+5;foo(b)

        for (auto OI = cur_it->op_begin(), OE = cur_it->op_end(); OI != OE; ++OI){
            Value *V = OI->get();
            Type *type = V->getType();
            if(type->isIntegerTy(32) || type->isIntegerTy(64)){
                AllocaInst *alloca = new AllocaInst(type, 0, (V->getName().str()+".alias"), &*cur_it);
                /*StoreInst *str = */new StoreInst(V, alloca, &*cur_it);
                LoadInst *ld = new LoadInst(alloca, (V->getName().str()+".ld"), &*cur_it);
                *OI = (Value*) ld;
                instrumented_insts += 3;//add 'alloca', 'store' and 'load'
            }/*
            else if(Instruction* oinst = dyn_cast<Instruction>(&*OI)){
                if(oinst->isBinaryOp() ||
                   (oinst->getOpcode() == Instruction::Call) ||
                   isHandledCoveInst(oinst) ||
                   (oinst->getOpcode() == Instruction::PHI) ||
                   (oinst->getOpcode() == Instruction::Select) ||
                        (oinst->getOpcode() == Instruction::Load)){
                    AllocaInst *alloca = new AllocaInst(oinst->getType(), 0, (oinst->getName().str()+".ptr"), &*cur_it);
                    new StoreInst(oinst, alloca, &*cur_it);
                    LoadInst *ld = new LoadInst(alloca, (oinst->getName().str()+".ld"), &*cur_it);
                    *OI = (Value*) ld;
                    instrumented_insts += 3;

                }
            }
            */

        }

        Function* precallfunc = TheModule->getFunction("__accmut__prepare_call");

        if (!precallfunc) {
            std::vector<Type*>FuncTy_3_args;
            FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
            FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
            FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
            FunctionType* FuncTy_3 = FunctionType::get(
                    /*Result=*/IntegerType::get(TheModule->getContext(), 32),
                    /*Params=*/FuncTy_3_args,
                    /*isVarArg=*/true);
            precallfunc = Function::Create(
                    /*Type=*/FuncTy_3,
                    /*Linkage=*/GlobalValue::ExternalLinkage,
                    /*Name=*/"__accmut__prepare_call", TheModule); // (external, no body)
            precallfunc->setCallingConv(CallingConv::C);
        }
        AttributeList func___accmut__prepare_call_PAL;
        {
            SmallVector<AttributeList, 4> Attrs;
            AttributeList PAS;
            {
                AttrBuilder B;
                PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
            }

            Attrs.push_back(PAS);
            func___accmut__prepare_call_PAL = AttributeList::get(TheModule->getContext(), Attrs);

        }
        precallfunc->setAttributes(func___accmut__prepare_call_PAL);


        std::vector<Value*> params;
        std::stringstream ss;
        ss<<mut_from;
        ConstantInt* from_i32= ConstantInt::get(TheModule->getContext(),
                                                APInt(32, StringRef(ss.str()), 10));
        params.push_back(from_i32);
        ss.str("");
        ss<<mut_to;
        ConstantInt* to_i32= ConstantInt::get(TheModule->getContext(),
                                              APInt(32, StringRef(ss.str()), 10));
        params.push_back(to_i32);

        int index = 0;
        int record_num = 0;
        std::vector<int> pushed_param_idx;

        //get signature info
        for (auto OI = cur_it->op_begin(), OE = cur_it->op_end() - 1; OI != OE; ++OI, ++index){

            Value *v = OI->get();
            Type *type = v->getType();
            if(!(type->isIntegerTy(32) || type->isIntegerTy(64)))
            {
                continue;
            }
            bool succ = pushPreparecallParam(params, index, v, TheModule);

            if(succ){
                pushed_param_idx.push_back(index);
                record_num++;
            }
            else{
                assert(false);
                //ERRMSG("---- WARNNING : PUSH PARAM FAILURE ");
                //VALERRMSG(cur_it,"CUR_OPRAND",v);
            }

        }

        if(! pushed_param_idx.empty()){
            llvm::errs()<<"---- PUSH PARAM : ";
            for(auto it = pushed_param_idx.begin(), ie = pushed_param_idx.end(); it != ie; ++it){
                llvm::errs()<<*it<<"'th\t";
            }
            llvm::errs()<<"\n";
        }

        //insert num of param-records
        ss.str("");
        ss<<record_num;
        ConstantInt* rcd = ConstantInt::get(TheModule->getContext(),
                                            APInt(32, StringRef(ss.str()), 10));
        params.insert( params.begin()+2 , rcd);

        CallInst *pre = CallInst::Create(precallfunc, params, "", &*cur_it);
        pre->setCallingConv(CallingConv::C);
        pre->setTailCall(false);
        AttributeList preattrset;
        pre->setAttributes(preattrset);

        ConstantInt* zero = ConstantInt::get(Type::getInt32Ty(TheModule->getContext()), 0);

        ICmpInst *hasstd = new ICmpInst(&*cur_it, ICmpInst::ICMP_EQ, pre, zero, "hasstd.call");

        BasicBlock *cur_bb = cur_it->getParent();

        Instruction* oricall = cur_it->clone();

        BasicBlock* label_if_end = cur_bb->splitBasicBlock(cur_it, "stdcall.if.end");

        BasicBlock* label_if_then = BasicBlock::Create(TheModule->getContext(), "stdcall.if.then",cur_bb->getParent(), label_if_end);
        BasicBlock* label_if_else = BasicBlock::Create(TheModule->getContext(), "stdcall.if.else",cur_bb->getParent(), label_if_end);

        /*
        cur_bb->back().eraseFromParent();

        BranchInst::Create(label_if_then, label_if_else, hasstd, cur_bb);
         */
        ReplaceInstWithInst(&cur_bb->back(), BranchInst::Create(label_if_then, label_if_else, hasstd));

        //errs() << "Basic block splitted\n";
        //label_if_then
        //move the loadinsts of params into if_then_block
        index = 0;
        for (auto OI = oricall->op_begin(), OE = oricall->op_end() - 1; OI != OE; ++OI, ++index){

            //only move pushed parameters
            if(find(pushed_param_idx.begin(), pushed_param_idx.end(), index) == pushed_param_idx.end()){
                continue;
            }

            if(LoadInst *ld = dyn_cast<LoadInst>(&*OI)){
                ld->removeFromParent();
                label_if_then->getInstList().push_back(ld);
            }else if(/*Constant *con = */dyn_cast<Constant>(&*OI)){
                // TODO::  test
                assert(false);
                continue;
            }else if(/*GetElementPtrInst *ge = */dyn_cast<GetElementPtrInst>(&*OI)){

                assert(false);
                // TODO: test
                //ge->removeFromParent();
                //label_if_then->getInstList().push_back(ge);
            }else{
                assert(false);
                // TODO:: check
                // TODO:: instrumented_insts !!!
                Instruction *coversion = dyn_cast<Instruction>(&*OI);
                if(isHandledCoveInst(coversion)){
                    Instruction* op_of_cov = dyn_cast<Instruction>(coversion->getOperand(0));
                    if(dyn_cast<LoadInst>(&*op_of_cov) || dyn_cast<AllocaInst>(&*op_of_cov)){
                        op_of_cov->removeFromParent();
                        label_if_then->getInstList().push_back(op_of_cov);
                        coversion->removeFromParent();
                        label_if_then->getInstList().push_back(coversion);
                    }else{
                        ERRMSG("CAN MOVE GET A POINTER INTO IF.THEN");
                        Value *v = dyn_cast<Value>(&*OI);
                        VALERRMSG(cur_it,"CUR_OPRAND",v);
                        exit(0);
                    }
                }else{
                    ERRMSG("CAN MOVE GET A POINTER INTO IF.THEN");
                    Value *v = dyn_cast<Value>(&*OI);
                    VALERRMSG(cur_it,"CUR_OPRAND",v);
                    exit(0);
                }
            }
        }
        label_if_then->getInstList().push_back(oricall);
        BranchInst::Create(label_if_end, label_if_then);
        //errs() << "if_then end\n";

        //label_if_else
        Function *std_handle;
        if(oricall->getType()->isIntegerTy(32)){
            std_handle = TheModule->getFunction("__accmut__stdcall_i32");

            if (!std_handle) {
                std::vector<Type*>FuncTy_0_args;
                FunctionType* FuncTy_0 = FunctionType::get(
                        /*Result=*/IntegerType::get(TheModule->getContext(), 32),
                        /*Params=*/FuncTy_0_args,
                        /*isVarArg=*/false);
                std_handle = Function::Create(
                        /*Type=*/FuncTy_0,
                        /*Linkage=*/GlobalValue::ExternalLinkage,
                        /*Name=*/"__accmut__stdcall_i32", TheModule); // (external, no body)
                std_handle->setCallingConv(CallingConv::C);
            }
            AttributeList func___accmut__stdcall_i32_PAL;
            {
                SmallVector<AttributeList, 4> Attrs;
                AttributeList PAS;
                {
                    AttrBuilder B;
                    PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                }

                Attrs.push_back(PAS);
                func___accmut__stdcall_i32_PAL = AttributeList::get(TheModule->getContext(), Attrs);

            }
            std_handle->setAttributes(func___accmut__stdcall_i32_PAL);


        }else if(oricall->getType()->isIntegerTy(64)){
            std_handle = TheModule->getFunction("__accmut__stdcall_i64");
            if (!std_handle) {
                std::vector<Type*>FuncTy_6_args;
                FunctionType* FuncTy_6 = FunctionType::get(
                        /*Result=*/IntegerType::get(TheModule->getContext(), 64),
                        /*Params=*/FuncTy_6_args,
                        /*isVarArg=*/false);
                std_handle = Function::Create(
                        /*Type=*/FuncTy_6,
                        /*Linkage=*/GlobalValue::ExternalLinkage,
                        /*Name=*/"__accmut__stdcall_i64", TheModule); // (external, no body)
                std_handle->setCallingConv(CallingConv::C);
            }
            AttributeList func___accmut__stdcall_i64_PAL;
            {
                SmallVector<AttributeList, 4> Attrs;
                AttributeList PAS;
                {
                    AttrBuilder B;
                    PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                }

                Attrs.push_back(PAS);
                func___accmut__stdcall_i64_PAL = AttributeList::get(TheModule->getContext(), Attrs);

            }
            std_handle->setAttributes(func___accmut__stdcall_i64_PAL);


        }else if(oricall->getType()->isVoidTy()){
            std_handle = TheModule->getFunction("__accmut__stdcall_void");
            if (!std_handle) {

                std::vector<Type*>FuncTy_8_args;
                FunctionType* FuncTy_8 = FunctionType::get(
                        /*Result=*/Type::getVoidTy(TheModule->getContext()),
                        /*Params=*/FuncTy_8_args,
                        /*isVarArg=*/false);

                std_handle = Function::Create(
                        /*Type=*/FuncTy_8,
                        /*Linkage=*/GlobalValue::ExternalLinkage,
                        /*Name=*/"__accmut__stdcall_void", TheModule); // (external, no body)
                std_handle->setCallingConv(CallingConv::C);
            }
            AttributeList func___accmut__stdcall_void_PAL;
            {
                SmallVector<AttributeList, 4> Attrs;
                AttributeList PAS;
                {
                    AttrBuilder B;
                    PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                }

                Attrs.push_back(PAS);
                func___accmut__stdcall_void_PAL = AttributeList::get(TheModule->getContext(), Attrs);

            }
            std_handle->setAttributes(func___accmut__stdcall_void_PAL);

        }else{
            ERRMSG("ERR CALL TYPE ");
            oricall->dump();
            oricall->getType()->dump();
            exit(0);
        }//}else if(oricall->getType()->isPointerTy()){


        CallInst*  stdcall = CallInst::Create(std_handle, "", label_if_else);
        stdcall->setCallingConv(CallingConv::C);
        stdcall->setTailCall(false);
        AttributeList stdcallPAL;
        stdcall->setAttributes(stdcallPAL);
        BranchInst::Create(label_if_end, label_if_else);

        //errs() << "if_else end\n";
        //label_if_end
        if(oricall->getType()->isVoidTy()){
            cur_it->eraseFromParent();
            instrumented_insts += 6;
        }
        else{
            PHINode* call_res = PHINode::Create(oricall->getType(), 2, "call.phi");
            call_res->addIncoming(oricall, label_if_then);
            call_res->addIncoming(stdcall, label_if_else);
            ReplaceInstWithInst(&*cur_it, call_res);
            instrumented_insts += 7;
        }

    }

    // StoreInst Navigation
    else if(StoreInst* st = dyn_cast<StoreInst>(&*cur_it)){
        // TODO:: add or call inst?
        if(ConstantInt* cons = dyn_cast<ConstantInt>(st->getValueOperand())){
            AllocaInst *alloca = new AllocaInst(cons->getType(), 0, "cons_alias", st);
            /*StoreInst *str = */new StoreInst(cons, alloca, st);
            LoadInst *ld = new LoadInst(alloca, "const_load", st);
            User::op_iterator OI = st->op_begin();
            *OI = (Value*) ld;
            instrumented_insts += 3;
        }

        Function* prestfunc;
        if(st->getValueOperand()->getType()->isIntegerTy(32)){
            prestfunc = TheModule->getFunction("__accmut__prepare_st_i32");
            if(!prestfunc){
                PointerType* PointerTy_1 = PointerType::get(IntegerType::get(TheModule->getContext(), 32), 0);
                std::vector<Type*> FuncTy_4_args;
                FuncTy_4_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                FuncTy_4_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                FuncTy_4_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                FuncTy_4_args.push_back(PointerTy_1);
                FunctionType* f_tp = FunctionType::get(IntegerType::get(TheModule->getContext(), 32),
                                                       FuncTy_4_args, false);
                prestfunc = Function::Create(f_tp, GlobalValue::ExternalLinkage,
                                             "__accmut__prepare_st_i32", TheModule); // (external, no body)
                prestfunc->setCallingConv(CallingConv::C);
            }

            AttributeList prestfunc_PAL;
            {
                SmallVector<AttributeList, 4> Attrs;
                AttributeList PAS;
                {
                    AttrBuilder B;
                    PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                }

                Attrs.push_back(PAS);
                prestfunc_PAL = AttributeList::get(TheModule->getContext(), Attrs);

            }
            prestfunc->setAttributes(prestfunc_PAL);

        }
        else if(st->getValueOperand()->getType()->isIntegerTy(64)){
            prestfunc = TheModule->getFunction("__accmut__prepare_st_i64");
            if (!prestfunc) {

                PointerType* PointerTy_2 = PointerType::get(IntegerType::get(TheModule->getContext(), 64), 0);

                std::vector<Type*>FuncTy_6_args;
                FuncTy_6_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                FuncTy_6_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                FuncTy_6_args.push_back(IntegerType::get(TheModule->getContext(), 64));
                FuncTy_6_args.push_back(PointerTy_2);
                FunctionType* FuncTy_6 = FunctionType::get(IntegerType::get(TheModule->getContext(), 32),
                                                           FuncTy_6_args, false);



                prestfunc =  Function::Create(FuncTy_6, GlobalValue::ExternalLinkage,
                                              "__accmut__prepare_st_i64", TheModule);
                prestfunc->setCallingConv(CallingConv::C);
            }

            AttributeList pal;
            {
                SmallVector<AttributeList, 4> Attrs;
                AttributeList PAS;
                {
                    AttrBuilder B;
                    PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                }

                Attrs.push_back(PAS);
                pal = AttributeList::get(TheModule->getContext(), Attrs);

            }
            prestfunc->setAttributes(pal);


        }else{
            ERRMSG("ERR STORE TYPE ");
            VALERRMSG(cur_it, "CUR_TYPE", st->getValueOperand()->getType());
            exit(0);
        }


        std::vector<Value*> params;
        std::stringstream ss;
        ss<<mut_from;
        ConstantInt* from_i32= ConstantInt::get(TheModule->getContext(),
                                                APInt(32, StringRef(ss.str()), 10));
        params.push_back(from_i32);
        ss.str("");
        ss<<mut_to;
        ConstantInt* to_i32= ConstantInt::get(TheModule->getContext(),
                                              APInt(32, StringRef(ss.str()), 10));
        params.push_back(to_i32);

        Value* tobestored = dyn_cast<Value>(st->op_begin());
        params.push_back(tobestored);

        auto addr = st->op_begin() + 1;// the pointer of the storeinst
        if(LoadInst *ld = dyn_cast<LoadInst>(&*addr)){//is a local var
            params.push_back(ld);
        }else if(AllocaInst *alloca = dyn_cast<AllocaInst>(&*addr)){
            params.push_back(alloca);
        }else if(Constant *con = dyn_cast<Constant>(&*addr)){
            params.push_back(con);
        }else if(GetElementPtrInst *gete = dyn_cast<GetElementPtrInst>(&*addr)){
            params.push_back(gete);
        }else{
            ERRMSG("NOT A POINTER ");
            cur_it->dump();
            Value *v = dyn_cast<Value>(&*addr);
            v->dump();
            exit(0);
        }
        CallInst *pre = CallInst::Create(prestfunc, params, "", &*cur_it);
        pre->setCallingConv(CallingConv::C);
        pre->setTailCall(false);
        AttributeList attrset;
        pre->setAttributes(attrset);

        ConstantInt* zero = ConstantInt::get(Type::getInt32Ty(TheModule->getContext()), 0);

        ICmpInst *hasstd = new ICmpInst(&*cur_it, ICmpInst::ICMP_EQ, pre, zero, "hasstd.st");

        BasicBlock *cur_bb = cur_it->getParent();

        BasicBlock* label_if_end = cur_bb->splitBasicBlock(cur_it, "stdst.if.end");

        BasicBlock* label_if_else = BasicBlock::Create(TheModule->getContext(), "std.st",cur_bb->getParent(), label_if_end);

        cur_bb->back().eraseFromParent();

        BranchInst::Create(label_if_end, label_if_else, hasstd, cur_bb);


        //label_if_else
        Function *std_handle = TheModule->getFunction("__accmut__std_store");

        if (!std_handle) {

            std::vector<Type*> args;
            FunctionType* ftp = FunctionType::get(Type::getVoidTy(TheModule->getContext()),
                                                  args,false);
            std_handle = Function::Create(ftp, GlobalValue::ExternalLinkage, "__accmut__std_store", TheModule); // (external, no body)
            std_handle->setCallingConv(CallingConv::C);
        }
        AttributeList func___accmut__std_store_PAL;
        {
            SmallVector<AttributeList, 4> Attrs;
            AttributeList PAS;
            {
                AttrBuilder B;
                PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
            }

            Attrs.push_back(PAS);
            func___accmut__std_store_PAL = AttributeList::get(TheModule->getContext(), Attrs);

        }
        std_handle->setAttributes(func___accmut__std_store_PAL);


        CallInst*  stdcall = CallInst::Create(std_handle, "", label_if_else);
        stdcall->setCallingConv(CallingConv::C);
        stdcall->setTailCall(false);
        AttributeList stdcallPAL;
        stdcall->setAttributes(stdcallPAL);
        BranchInst::Create(label_if_end, label_if_else);

        //label_if_end
        cur_it->eraseFromParent();
        instrumented_insts += 4;
    }
    // BinaryInst Navigation
    else{
        // FOR ARITH INST
        if(cur_it->getOpcode() >= Instruction::Add &&
           cur_it->getOpcode() <= Instruction::Xor){
            Type* ori_ty = cur_it->getType();
            Function* f_process;
            if(ori_ty->isIntegerTy(32)){
                if(aboutGoodVariable)
                {
                    f_process = goodVarI32ArithFunc;
                }
                else
                {
                    f_process = TheModule->getFunction("__accmut__process_i32_arith");

                    if (!f_process)
                    {
                        std::vector<Type *> ftp_args;
                        ftp_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        ftp_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        ftp_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        ftp_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FunctionType *ftp = FunctionType::get(IntegerType::get(TheModule->getContext(), 32), ftp_args,
                                                              false);
                        f_process = Function::Create(ftp, GlobalValue::ExternalLinkage,
                                                     "__accmut__process_i32_arith", TheModule); // (external, no body)
                        f_process->setCallingConv(CallingConv::C);
                    }
                    AttributeList func___accmut__process_i32_arith_PAL;
                    {
                        SmallVector<AttributeList, 4> Attrs;
                        AttributeList PAS;
                        {
                            AttrBuilder B;
                            PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                        }

                        Attrs.push_back(PAS);
                        func___accmut__process_i32_arith_PAL = AttributeList::get(TheModule->getContext(), Attrs);

                    }
                    f_process->setAttributes(func___accmut__process_i32_arith_PAL);
                }
            }else if(ori_ty->isIntegerTy(64)){
                if(aboutGoodVariable)
                {
                    f_process = goodVarI64ArithFunc;
                }
                else
                {
                    f_process = TheModule->getFunction("__accmut__process_i64_arith");

                    std::vector<Type *> ftp_args;
                    ftp_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                    ftp_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                    ftp_args.push_back(IntegerType::get(TheModule->getContext(), 64));
                    ftp_args.push_back(IntegerType::get(TheModule->getContext(), 64));
                    FunctionType *ftp = FunctionType::get(IntegerType::get(TheModule->getContext(), 64), ftp_args,
                                                          false);
                    if (!f_process)
                    {
                        f_process = Function::Create(ftp, GlobalValue::ExternalLinkage, "__accmut__process_i64_arith",
                                                     TheModule);
                        f_process->setCallingConv(CallingConv::C);
                    }
                    AttributeList pal;
                    {
                        SmallVector<AttributeList, 4> Attrs;
                        AttributeList PAS;
                        {
                            AttrBuilder B;
                            PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                        }

                        Attrs.push_back(PAS);
                        pal = AttributeList::get(TheModule->getContext(), Attrs);

                    }
                    f_process->setAttributes(pal);
                }
            }else{
                ERRMSG("ArithInst TYPE ERROR ");
                cur_it->dump();
                llvm::errs()<<*ori_ty<<"\n";
                // TODO:: handle i1, i8, i64 ... type
                exit(0);
            }

            std::vector<Value*> int_call_params;
            std::stringstream ss;
            ss<<mut_from;
            ConstantInt* from_i32= ConstantInt::get(TheModule->getContext(), APInt(32, StringRef(ss.str()), 10));
            int_call_params.push_back(from_i32);
            ss.str("");
            ss<<mut_to;
            ConstantInt* to_i32= ConstantInt::get(TheModule->getContext(), APInt(32, StringRef(ss.str()), 10));
            int_call_params.push_back(to_i32);
            int_call_params.push_back(cur_it->getOperand(0));
            int_call_params.push_back(cur_it->getOperand(1));

            if(aboutGoodVariable)
            {
                pushGoodVarIdInfo(int_call_params, cur_it);
            }

            CallInst *call = CallInst::Create(f_process, int_call_params);
            ReplaceInstWithInst(&*cur_it, call);


        }
            // FOR ICMP INST
            // Cmp Navigation
        else if(cur_it->getOpcode() == Instruction::ICmp){
            Function* f_process;

            if(cur_it->getOperand(0)->getType()->isIntegerTy(32)){
                if(aboutGoodVariable)
                {
                    f_process = goodVarI32CmpFunc;
                }
                else
                {
                    f_process = TheModule->getFunction("__accmut__process_i32_cmp");

                    if (!f_process)
                    {

                        std::vector<Type *> FuncTy_3_args;
                        FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FuncTy_3_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FunctionType *FuncTy_3 = FunctionType::get(
                                /*Result=*/IntegerType::get(TheModule->getContext(), 32),
                                /*Params=*/FuncTy_3_args,
                                /*isVarArg=*/false);

                        f_process = Function::Create(
                                /*Type=*/FuncTy_3,
                                /*Linkage=*/GlobalValue::ExternalLinkage,
                                /*Name=*/"__accmut__process_i32_cmp", TheModule); // (external, no body)
                        f_process->setCallingConv(CallingConv::C);
                    }
                    AttributeList func___accmut__process_i32_cmp_PAL;
                    {
                        SmallVector<AttributeList, 4> Attrs;
                        AttributeList PAS;
                        {
                            AttrBuilder B;
                            PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                        }

                        Attrs.push_back(PAS);
                        func___accmut__process_i32_cmp_PAL = AttributeList::get(TheModule->getContext(), Attrs);

                    }
                    f_process->setAttributes(func___accmut__process_i32_cmp_PAL);
                }
            }else if(cur_it->getOperand(0)->getType()->isIntegerTy(64)){
                if(aboutGoodVariable)
                {
                    f_process = goodVarI64CmpFunc;
                }
                else
                {
                    f_process = TheModule->getFunction("__accmut__process_i64_cmp");

                    if (!f_process)
                    {

                        std::vector<Type *> FuncTy_5_args;
                        FuncTy_5_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FuncTy_5_args.push_back(IntegerType::get(TheModule->getContext(), 32));
                        FuncTy_5_args.push_back(IntegerType::get(TheModule->getContext(), 64));
                        FuncTy_5_args.push_back(IntegerType::get(TheModule->getContext(), 64));
                        FunctionType *FuncTy_5 = FunctionType::get(
                                /*Result=*/IntegerType::get(TheModule->getContext(), 32),
                                /*Params=*/FuncTy_5_args,
                                /*isVarArg=*/false);


                        f_process = Function::Create(
                                /*Type=*/FuncTy_5,
                                /*Linkage=*/GlobalValue::ExternalLinkage,
                                /*Name=*/"__accmut__process_i64_cmp", TheModule); // (external, no body)
                        f_process->setCallingConv(CallingConv::C);
                    }
                    AttributeList func___accmut__process_i64_cmp_PAL;
                    {
                        SmallVector<AttributeList, 4> Attrs;
                        AttributeList PAS;
                        {
                            AttrBuilder B;
                            PAS = AttributeList::get(TheModule->getContext(), ~0U, B);
                        }

                        Attrs.push_back(PAS);
                        func___accmut__process_i64_cmp_PAL = AttributeList::get(TheModule->getContext(), Attrs);

                    }
                    f_process->setAttributes(func___accmut__process_i64_cmp_PAL);
                }
            }else{
                ERRMSG("ICMP TYPE ERROR ");
                exit(0);
            }

            std::vector<Value*> int_call_params;
            std::stringstream ss;
            ss<<mut_from;
            ConstantInt* from_i32= ConstantInt::get(TheModule->getContext(),
                                                    APInt(32, StringRef(ss.str()), 10));
            int_call_params.push_back(from_i32);
            ss.str("");
            ss<<mut_to;
            ConstantInt* to_i32= ConstantInt::get(TheModule->getContext(),
                                                  APInt(32, StringRef(ss.str()), 10));
            int_call_params.push_back(to_i32);
            int_call_params.push_back(cur_it->getOperand(0));
            int_call_params.push_back(cur_it->getOperand(1));

            if(aboutGoodVariable)
            {
                pushGoodVarIdInfo(int_call_params, cur_it);
                errs() << f_process->getFunctionType() << " " << int_call_params.size() << "\n";
            }

            CallInst *call = CallInst::Create(f_process, int_call_params, "", &*cur_it);
            CastInst* i32_conv = new TruncInst(call, IntegerType::get(TheModule->getContext(), 1), "");

            instrumented_insts++;

            ReplaceInstWithInst(&*cur_it, i32_conv);
        }
    }
}


void WAInstrumenter::getInstMutsMap(vector<Mutation*> *v, Function &F)
{
    instMutsMap.clear();

    int instIndex = 0;
    auto mutIter = v->begin();

    for(auto I = inst_begin(F), E = inst_end(F); I != E; ++I, ++instIndex)
    {
        /*
        if(F.getName().equals("unlzw"))
        {
            errs() << instIndex << "\t" << *I << "\n";
        }
         */

        while((*mutIter)->index == instIndex)
        {
            instMutsMap[&*I].push_back(*mutIter);

            ++mutIter;
            if(mutIter == v->end())
            {
                return;
            }
        }
    }
}


void WAInstrumenter::getGoodVariables(BasicBlock &BB)
{
    goodVariables.clear();

    int goodVariableCount = 0;

    for(auto I = BB.rbegin(), E = BB.rend(); I != E; ++I)
    {
        if(!isSupportedInstruction(&*I) || I->isUsedOutsideOfBlock(&BB))
        {
            //errs() << *I << " " << isSupportedInstruction(&*I) << " " << I->isUsedOutsideOfBlock(&BB) << "\n";
            continue;
        }

        //errs() << "GoodVar Candidates: " << *I << "\n";

        // All users should be good variables or bool instruction
        bool checkUser = true;

        for(User *U : I->users())
        {
            if (Instruction *userInst = dyn_cast<Instruction>(U))
            {
                if(!isSupportedBoolInstruction(userInst)
                //&& goodVariables.count(userInst) == 0
                && !isSupportedInstruction(userInst)
                        )
                {
                    checkUser = false;
                    break;
                }
            }
            else
            {
                assert(false);
            }
        }

        if(checkUser/* || (userCount == 1 && isSupportedInstruction(dyn_cast<Instruction>(*I->users().begin())))*/)
        {
            // assign an index to each good variable for instrument
            goodVariables[&*I] = ++goodVariableCount;
        }
    }
}


void WAInstrumenter::filterRealGoodVariables()
{
    for(auto it = goodVariables.begin(), end = goodVariables.end(); it != end;)
    {
        // avoid iterator invalidation
        Instruction *I = it->first;
        ++it;

        if(instMutsMap.count(I) == 0 && !hasUsedPreviousGoodVariables(I))
        {
            goodVariables.erase(I);
        }
    }
}


void WAInstrumenter::pushGoodVarIdInfo(vector<Value*> &params, Instruction *I)
{
    //errs() << "in push\n";
    params.push_back(getGoodVarId(I));
    params.push_back(getGoodVarId(I->getOperand(0)));
    params.push_back(getGoodVarId(I->getOperand(1)));
    params.push_back(getI32Constant(I->isBinaryOp() ? I->getOpcode() : (int)dyn_cast<ICmpInst>(I)->getPredicate()));
    //errs() << "out push\n";
}


ConstantInt* WAInstrumenter::getGoodVarId(Value *V)
{
    Instruction *I = dyn_cast<Instruction>(V);
    auto it = goodVariables.find(I);
    int id = it != goodVariables.end() ? it->second : -1;
    return getI32Constant(id);
}


ConstantInt* WAInstrumenter::getI32Constant(int value)
{
    return ConstantInt::get(TheModule->getContext(), APInt(32, std::to_string(value), 10));
}


bool WAInstrumenter::isSupportedOpcode(Instruction *I)
{
    unsigned opcode = I->getOpcode();
    return (Instruction::isBinaryOp(opcode) && Instruction::getOpcodeName(opcode)[0] != 'F')
           || opcode == Instruction::ICmp
           //|| opcode == Instruction::Select
            ;
}


bool WAInstrumenter::isSupportedOp(Instruction *I)
{
    // Opcode and Operands both supported?
    return isSupportedOpcode(I) && isSupportedType(I->getOperand(1));
}


bool WAInstrumenter::isSupportedInstruction(Instruction *I)
{
    return isSupportedType(I) && isSupportedOp(I);
}


bool WAInstrumenter::isSupportedBoolInstruction(Instruction *I)
{
    return (I->getType()->isIntegerTy(1) && isSupportedOp(I))
            //|| I->getOpcode() == Instruction::Select
            ;
}


bool WAInstrumenter::isSupportedType(Value *V)
{
    // TODO: only support i32 and i64 good variables now
    Type *T = V->getType();
    return T->isIntegerTy(32) || T->isIntegerTy(64);
}


bool WAInstrumenter::hasUsedPreviousGoodVariables(Instruction *I)
{
    for(Use &U : I->operands())
    {
        Instruction *usedInst = dyn_cast<Instruction>(U.get());
        if(goodVariables.count(usedInst) == 1)
        {
            //errs() << *usedInst << "\n";
            return true;
        }
    }
    return false;
}


char WAInstrumenter::ID = 0;
static RegisterPass<WAInstrumenter> X("AccMut-window", "AccMut - Instrument Code with Window Mechanism");