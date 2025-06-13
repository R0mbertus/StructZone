#include "redzone.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <stdio.h>

struct Runtime {
    Function *rdzone_add_f;
    Function *rdzone_check_f;
    Function *rdzone_rm_f;
};

/**
 * This function simply inserts some call to the runtime test function on the
 * first function it sees. This function is used to check if the runtime is correctly
 * linked and active.
 */
void add_runtime_test(Function *test_function, Module &M) {
    for (Function &F : M) {
        if (F.isDeclaration()) {
            continue;
        }

        outs() << F.getName() << "\n";
        for (BasicBlock &bb : F) {
            for (Instruction &I : bb) {
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
 * called by the program under test. This step is similar to the #include primitive in c
 *
 * The functions to include include:
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
struct Runtime add_runtime_linkage(Module &M) {

    // Construct argument lists
    SmallVector<Type *> test_runtime_args = {};
    SmallVector<Type *> rdzone_add_args = {PointerType::get(Type::getInt8Ty(M.getContext()), 0),
                                           Type::getInt64Ty(M.getContext())};
    SmallVector<Type *> rdzone_check_args = {PointerType::get(Type::getInt8Ty(M.getContext()), 0),
                                             Type::getInt8Ty(M.getContext())};
    SmallVector<Type *> rdzone_rm_args = {PointerType::get(Type::getInt8Ty(M.getContext()), 0)};

    // Function types
    FunctionType *test_runtime_t = FunctionType::get(Type::getVoidTy(M.getContext()),
                                                     ArrayRef<Type *>(test_runtime_args), false);
    FunctionType *rdzone_add_t = FunctionType::get(Type::getVoidTy(M.getContext()),
                                                   ArrayRef<Type *>(rdzone_add_args), false);
    FunctionType *rdzone_check_t = FunctionType::get(Type::getVoidTy(M.getContext()),
                                                     ArrayRef<Type *>(rdzone_check_args), false);
    FunctionType *rdzone_rm_t =
        FunctionType::get(Type::getVoidTy(M.getContext()), ArrayRef<Type *>(rdzone_rm_args), false);

    // FunctionCallee prototype = M.getOrInsertFunction("test_runtime_link", f);
    Function *test_runtime_f =
        Function::Create(test_runtime_t, Function::ExternalLinkage, "test_runtime_link", M);
    Function *rdzone_add_f =
        Function::Create(rdzone_add_t, Function::ExternalLinkage, "__rdzone_add", M);
    Function *rdzone_check_f =
        Function::Create(rdzone_check_t, Function::ExternalLinkage, "__rdzone_check", M);
    Function *rdzone_rm_f =
        Function::Create(rdzone_rm_t, Function::ExternalLinkage, "__rdzone_rm", M);

    struct Runtime runtime = {};
    runtime.rdzone_add_f = rdzone_add_f;
    runtime.rdzone_check_f = rdzone_check_f;
    runtime.rdzone_rm_f = rdzone_rm_f;

    add_runtime_test(test_runtime_f, M);
    return runtime;
}

/**
 * Helper function that finds all the returns in the function and instruments them.
 * @param returns the output for a list of returns.
 * @param function the functions to search for return instructions
 */
void findReturnInsts(SmallVector<ReturnInst *> *returns, Function *function) {
    for (BasicBlock &BB : *function) {
        for (Instruction &ins : BB) {
            if (ReturnInst *ret = dyn_cast<ReturnInst>(&ins)) {
                returns->push_back(ret);
            }
        }
    }
}

/**
 * Function that instruments stack structs with redzone initialiser and de-initialiser
 * functions.
 * @param allocaInst the alloca corresponding to the stack struct.
 * @param runtime The collection of linked runtime functions.
 */
void initialiseAllocaStruct(AllocaInst *allocaInst, Runtime *runtime,
                            std::map<StringRef, std::shared_ptr<StructInfo>> *redzoneInfo) {
    LLVMContext *C = &allocaInst->getContext();
    IRBuilder<> builder(*C);
    
    StringRef inflatedStructName = allocaInst->getAllocatedType()->getStructName();
    std::shared_ptr<StructInfo> structInfo = redzoneInfo->at(inflatedStructName);

    SmallVector<ReturnInst *> functionExits = {};
    findReturnInsts(&functionExits, allocaInst->getParent()->getParent());

    for (size_t i : structInfo.get()->redzone_offsets)
    {
        // create GEP to get pointer to redzone
        outs() << "    + " << i << "\n";
        builder.SetInsertPoint(allocaInst->getNextNode());
        SmallVector<Value *> indeces = {ConstantInt::get(IntegerType::getInt32Ty(*C), 0, false),
                                        ConstantInt::get(IntegerType::getInt32Ty(*C), i, false)};
        Value *redzone_addr = builder.CreateGEP(allocaInst->getAllocatedType(), allocaInst, indeces);

        // create CALL to void @__rdzone_add(i8*, i64)
        SmallVector<Value *> argsAdd = {
            builder.CreateBitCast(redzone_addr, PointerType::get(IntegerType::getInt8Ty(*C), 0)),
            ConstantInt::get(IntegerType::getInt64Ty(*C), REDZONE_SIZE, false)};
        builder.CreateCall(runtime->rdzone_add_f, argsAdd);

        // create CALL to void @__rdzone_rm(i8*)
        SmallVector<Value *> argsRm = {argsAdd[0]};
        for(ReturnInst *ret : functionExits){
            builder.SetInsertPoint(ret);
            builder.CreateCall(runtime->rdzone_rm_f, argsRm);
        }
    }
    
}

/**
 * Instrument loads or stores with access checks.
 * @param ins Instruction to insert above (typically load or store)
 * @param ptrOperand The address to check.
 * @param acessedType The type of variable that is loaded (so we understand how large the load is)
 * @param runtime The collection of runtime functions to insert.
 */
void insertMemAccessCheck(Instruction *ins, Value *ptrOperand, Type *accessedType,
                          Runtime *runtime) {
    assert(ptrOperand && ins);
    LLVMContext* C = &ins->getContext();
    IRBuilder<> builder(*C);
    builder.SetInsertPoint(ins);
    // find a way to get the ptr type of the operand.
    // cast it to a i8*
    // insert a call to __rdzone_check

    //cast our pointer to i8*
    PointerType *rawPtrTy = dyn_cast<PointerType>(ptrOperand->getType());
    assert(rawPtrTy);
    PointerType *targetPtrTy = IntegerType::getInt8PtrTy(ins->getContext());
    Value* castedPtr = builder.CreateBitCast(ptrOperand, targetPtrTy);
    
    PointerType *ptr_ty = IntegerType::getInt8PtrTy(ins->getContext());
    SmallVector<Value *> one = {
        ConstantInt::get(IntegerType::getInt8Ty(ins->getContext()), APInt(8, 1))};

    /*
    Create a typed null pointer. Index it by 1.
    */
    ConstantPointerNull *typedNullPtr = ConstantPointerNull::get(PointerType::get(accessedType, 0));
    Value* sizeofPtr = builder.CreateGEP(accessedType, typedNullPtr, one);
    Value* sizeofInt = builder.CreatePtrToInt(sizeofPtr, IntegerType::getInt8Ty(ins->getContext()));

    SmallVector<Value *> args = {castedPtr, sizeofInt};

    builder.CreateCall(runtime->rdzone_check_f, args);
}

/**
 * Takes a module with inflated structs, and sets up actual redzones in the inflations.
 * @param redzoneInfo Should contain information about which struct fields are redzones
 * in the form of `structName` -> fieldIndex
 * @param M the module to instrument. This should already contain all inflated structs
 */
void setupRedzones(std::map<StringRef, std::shared_ptr<StructInfo>> *redzoneInfo, Module &M) {
    struct Runtime runtime = add_runtime_linkage(M);
    for (Function &func : M) {
        for (BasicBlock &bb : func) {
            for (Instruction &inst : bb) {
                AllocaInst *alloca_inst = dyn_cast<AllocaInst>(&inst);
                if (alloca_inst && alloca_inst->getAllocatedType()->isStructTy()) {
                    initialiseAllocaStruct(alloca_inst, &runtime, redzoneInfo);
                    continue;
                }

                LoadInst *loadInst = dyn_cast<LoadInst>(&inst);
                if (loadInst) {
                    assert(loadInst->getType()->isSized());
                    insertMemAccessCheck(&inst, loadInst->getOperand(0), loadInst->getType(),
                                         &runtime);
                    continue;
                }

                StoreInst *storeInst = dyn_cast<StoreInst>(&inst);
                if (storeInst) {
                    insertMemAccessCheck(&inst, storeInst->getOperand(1),
                                         storeInst->getOperand(0)->getType(), &runtime);
                    continue;
                }
            }
        }
    }
}

void refactor_structinfo(std::map<Type *, std::shared_ptr<StructInfo>> *structInfo,
                         std::map<StringRef, std::shared_ptr<StructInfo>> *redzoneInfo) {
    for (std::pair<Type *, std::shared_ptr<StructInfo>> i : *structInfo) {
        redzoneInfo->insert({i.second.get()->inflatedType->getName(), i.second});
    }
}

void setupRedzoneChecks(std::map<Type *, std::shared_ptr<StructInfo>> *info, Module &M) {
    std::map<StringRef, std::shared_ptr<StructInfo>> redzoneInfo;
    refactor_structinfo(info, &redzoneInfo);
    setupRedzones(&redzoneInfo, M);
}