#include "redzone.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include <stdio.h>

struct Runtime
{
    Function* rdzone_add_f;
    Function* rdzone_check_f;
    Function* rdzone_rm_f;
};


void add_runtime_test(Function* test_function, Module &M){
    for (Function &F : M)
    {
        if (F.isDeclaration())
        {
            continue;
        }
        
        outs() << F.getName() << "\n";
        for(BasicBlock &bb : F){
            for (Instruction &I : bb)
            {
                CallInst *newCall = CallInst::Create(test_function, "", &I);
                outs() << "call : " << *newCall << "\n";
                break;
            }
            break;
        }
        break;
    }
}

/**
 * This func adds the required functions into the LLVM module so they can be
 * called by the program under test. The functions include:
 *  __rdzone_add {void @__rdzone_add(i8* noundef %0, i64 noundef %1)} 
 *  __rdzone_check {void @__rdzone_check(i8* noundef %0, i8 noundef zeroext %1)} 
 *  __rdzone_dbg_print {void @__rdzone_dbg_print()}
 *  __rdzone_reset {void @__rdzone_reset()}
 *  __rdzone_rm {void @__rdzone_rm(i8* noundef %0)
 * 
 * __rdzone_dbg_print (prints the AVL tree)
 * __rdzone_reset (removes all redzones)
 * __rdzone_check (checks ptr for safe access)
 * __rdzone_add (deletes all redzones)
 * __rdzone_rm (removes a redzone)
 */
struct Runtime add_runtime_linkage(Module &M){
    SmallVector<Type*> test_runtime_args = {};
    SmallVector<Type*> rdzone_add_args = {
        PointerType::get(Type::getInt8Ty(M.getContext()), 0),
        Type::getInt64Ty(M.getContext())
    };
    SmallVector<Type*> rdzone_check_args = {
        PointerType::get(Type::getInt8Ty(M.getContext()), 0),
        Type::getInt8Ty(M.getContext())
    };
    SmallVector<Type*> rdzone_rm_args = {
        PointerType::get(Type::getInt8Ty(M.getContext()), 0)
    };

    FunctionType *test_runtime_t = FunctionType::get(Type::getVoidTy(M.getContext()), 
        ArrayRef<Type*>(test_runtime_args), false);
    FunctionType *rdzone_add_t = FunctionType::get(Type::getVoidTy(M.getContext()), 
        ArrayRef<Type*>(rdzone_add_args), false);
    FunctionType *rdzone_check_t = FunctionType::get(Type::getVoidTy(M.getContext()), 
        ArrayRef<Type*>(rdzone_check_args), false);
    FunctionType *rdzone_rm_t = FunctionType::get(Type::getVoidTy(M.getContext()), 
        ArrayRef<Type*>(rdzone_rm_args), false);
    
    // FunctionCallee prototype = M.getOrInsertFunction("test_runtime_link", f);
    Function* test_runtime_f = Function::Create(test_runtime_t, Function::ExternalLinkage, "test_runtime_link", M);
    Function* rdzone_add_f = Function::Create(rdzone_add_t, Function::ExternalLinkage, "__rdzone_add", M);
    Function* rdzone_check_f = Function::Create(rdzone_check_t, Function::ExternalLinkage, "__rdzone_check", M);
    Function* rdzone_rm_f = Function::Create(rdzone_rm_t, Function::ExternalLinkage, "__rdzone_rm", M);

    struct Runtime runtime = {};
    runtime.rdzone_add_f = rdzone_add_f;
    runtime.rdzone_check_f = rdzone_check_f;
    runtime.rdzone_rm_f = rdzone_rm_f;
    
    add_runtime_test(test_runtime_f, M);
    return runtime;
}

void initialiseAllocaStruct(AllocaInst* allocaInst, Runtime* runtime){
    
}

void insertMemAccessCheck(Instruction* ins, Value* ptrOperand, Type* accessedType, Runtime* runtime){
    assert(ptrOperand && ins);
    // find a way to get the ptr type of the operand.
    // cast it to a i8* 
    // insert a call to __rdzone_check

    PointerType* rawPtrTy = dyn_cast<PointerType>(ptrOperand->getType());
    assert(rawPtrTy);
    PointerType* targetPtrTy = IntegerType::getInt8PtrTy(ins->getContext());    
    CastInst* castedPtr = BitCastInst::CreatePointerCast(ptrOperand, targetPtrTy, "", ins);
    outs() << "    castedPtr = " << *castedPtr << "\n";
    
    PointerType* ptr_ty = IntegerType::getInt8PtrTy(ins->getContext());
    SmallVector<Value*> one = {ConstantInt::get(IntegerType::getInt8Ty(ins->getContext()), APInt(8, 1))};

    /*
    Create a typed null pointer. Index it by 1.
    */
    ConstantPointerNull* typedNullPtr = ConstantPointerNull::get(PointerType::get(accessedType, 0));
    outs() << "    typedNPtr = " << *typedNullPtr << "\n";
    GetElementPtrInst* sizeofPtr = GetElementPtrInst::Create(accessedType, typedNullPtr, one, "", ins);
    outs() << "    sizeofPtr = " << *sizeofPtr << "\n";
    CastInst* sizeofInt = CastInst::CreatePointerCast(sizeofPtr, IntegerType::getInt8Ty(ins->getContext()), "", ins);
    outs() << "    sizeofInt = " << *sizeofInt << "\n";
    // Value* widthVal = ConstantInt::get(IntegerType::getInt8Ty(ins->getContext()), width);
    
    SmallVector<Value*> args = {
        castedPtr,
        sizeofInt
    };

    CallInst *newCall = CallInst::Create(runtime->rdzone_check_f, args, "", ins);
    outs() << "    call = " << *newCall << "\n";
}

void setupRedzones(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M){
    struct Runtime runtime = add_runtime_linkage(M);
    for (Function &func : M){
        for(BasicBlock &bb : func){
            for(Instruction &inst : bb){
                AllocaInst* alloca_inst = dyn_cast<AllocaInst>(&inst);
                if (alloca_inst && alloca_inst->getType()->isStructTy())
                {
                    initialiseAllocaStruct(alloca_inst, &runtime);
                    continue;
                }

                LoadInst* loadInst = dyn_cast<LoadInst>(&inst);
                if(loadInst){
                    outs() << *loadInst << "\n";
                    assert(loadInst->getType()->isSized());
                    insertMemAccessCheck(&inst, loadInst->getOperand(0), 
                        loadInst->getType() ,&runtime);
                    continue;
                }
                
                StoreInst* storeInst = dyn_cast<StoreInst>(&inst);
                if(storeInst){
                    outs() << *storeInst << "\n";
                    insertMemAccessCheck(&inst, storeInst->getOperand(1), 
                        storeInst->getOperand(0)->getType() ,&runtime);
                    continue;
                }
            }
        }
    }
}

void setupRedzoneChecks(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M){
    setupRedzones(info, M);
}