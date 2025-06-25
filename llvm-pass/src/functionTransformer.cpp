#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdarg>
#include <map>

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "functionTransformer.h"
#include "redzone.h"
#include "utils.h"

Type *getInflatedType(Type *arg_type, StructMap *struct_mapping, bool *changed = NULL) {
    int pointer_layers = 0;
    Type *arg_type_cp = arg_type;
    while (arg_type_cp->isPointerTy()) {
        pointer_layers++;
        assert(!arg_type_cp->isOpaquePointerTy());
        arg_type_cp = arg_type_cp->getPointerElementType();
    }
    if (arg_type_cp->isStructTy()) {
        if (changed != NULL) {
            *changed = true;
        }
        arg_type_cp = struct_mapping->at(arg_type_cp).get()->inflatedType;
    } else if (auto *func_type = dyn_cast<FunctionType>(arg_type_cp)) {
        // Re-create the function pointer type to include inflated struct types.
        auto *new_ret = getInflatedType(func_type->getReturnType(), struct_mapping, changed);
        std::vector<Type *> new_args;
        for (int i = 0; i < func_type->getNumParams(); i++) {
            new_args.push_back(
                getInflatedType(func_type->getParamType(i), struct_mapping, changed));
        }
        arg_type_cp = FunctionType::get(new_ret, new_args, func_type->isVarArg());
    }
    for (int i = 0; i < pointer_layers; i++) {
        arg_type_cp = arg_type_cp->getPointerTo();
    }
    return arg_type_cp;
}

void rebuildCalls(Module *M, Function *oldFunc, Function *newFunc) {
    std::map<CallInst *, std::tuple<Function *, std::vector<Value *>>> call_mapping;
    for (Function &F : *M) {
        for (BasicBlock &bb : F) {
            for (Instruction &inst : bb) {
                CallInst *callInst = dyn_cast<CallInst>(&inst);
                if (callInst && callInst->getCalledFunction() == oldFunc) {
                    std::vector<llvm::Value *> args;
                    for (unsigned i = 0; i < callInst->arg_size(); ++i) {
                        args.push_back(callInst->getArgOperand(i));
                    }
                    call_mapping[callInst] = std::make_tuple(newFunc, args);
                }
            }
        }
    }
    IRBuilder<> builder(M->getContext());
    for (const auto &[callInst, tup] : call_mapping) {
        builder.SetInsertPoint(callInst);
        auto *newCall = builder.CreateCall(std::get<0>(tup)->getFunctionType(), std::get<0>(tup),
                                           std::get<1>(tup));
        callInst->replaceAllUsesWith(newCall);
        callInst->eraseFromParent();
    }
}

void addDeflationToLibFunc(Function *func) {
    // this is a function called by us
}

void addDeflationToExternal(Function *func) {}

// this function is mostly here to make sure the module is still valid after
// transformation. It converts any struct args to inflated args

Function *createInflatedEmpty(Function *original, StructMap *struct_mapping, bool *hasStructArgs) {
    SmallVector<Type *> newArgs;

    // build the new function type
    for (Argument *a = original->arg_begin(); a < original->arg_end(); a++) {
        Type *newArg = getInflatedType(a->getType(), struct_mapping, hasStructArgs);
        newArgs.push_back(newArg);
    }

    FunctionType *inflatedFuncType =
        FunctionType::get(getInflatedType(original->getReturnType(), struct_mapping, hasStructArgs),
                          newArgs, original->isVarArg());
    Function *newFunc = Function::Create(inflatedFuncType, Function::InternalLinkage,
                                         original->getName() + ".inflated", original->getParent());

    // replace the calls
    rebuildCalls(original->getParent(), original, newFunc);
    original->replaceAllUsesWith(newFunc);
    return newFunc;
}

std::map<Function *, Function *> exportedFuncsToWrap; // inflated to original
std::map<Function *, Function *> libraryFuncsToWrap;  // inflated to original

Function *makeInflatedClone(Function *original, StructMap *struct_mapping) {
    bool hasStructArgs;
    ValueToValueMapTy map;

    Function *newFunc = createInflatedEmpty(original, struct_mapping, &hasStructArgs);
    errs() << "exported func: " << original->getName() << " has a twin: " << newFunc->getName()
           << "\n";
    for (size_t i = 0; i < newFunc->arg_size(); i++) {
        errs() << "arg " << i << " is ";
        original->getArg(i)->print(errs());
        errs() << "\t";
        original->getArg(i)->getType()->print(errs());
        errs() << "\n";
        map.insert({original->getArg(i), newFunc->getArg(i)});
    }
    SmallVector<ReturnInst *> returns;
    CloneFunctionInto(newFunc, original, map, CloneFunctionChangeType::LocalChangesOnly, returns);
    original->deleteBody();

    exportedFuncsToWrap.insert({newFunc, original});
    // clone original
    // replace calls to the original function with the new one
    // delete the old body

    // *DELAYED* make the old body be an inflation-wrapper to the new function
    return newFunc;
}

Function *makeInflatedWrapper(Function *original, StructMap *struct_mapping) {
    bool hasStructArgs;
    Function *newFunc = createInflatedEmpty(original, struct_mapping, &hasStructArgs);
    errs() << "library func: " << original->getName() << " has a twin " << newFunc->getName()
           << "\n";
    libraryFuncsToWrap.insert({newFunc, original});

    return newFunc;
}

void transformFuncSig(Function *original, StructMap *struct_mapping) {
    Function *replacement = NULL;
    if (original->isIntrinsic()) {
        return;
    } else if (original->isDeclaration()) {
        replacement = makeInflatedWrapper(original, struct_mapping);
    } else {
        replacement = makeInflatedClone(original, struct_mapping);
    }
}

Value *makeStructAccessGEP(IRBuilder<> *b, StructType *structTy, Value *ptrToStruct, int fieldIdx,
                           std::string annot = "") {
    LLVMContext *C = &b->getContext();
    SmallVector<Value *> indeces = {ConstantInt::get(IntegerType::getInt32Ty(*C), 0),
                                    ConstantInt::get(IntegerType::getInt32Ty(*C), fieldIdx)};
    errs() << "  GEP " << structTy->getName() << " " << *ptrToStruct << " [0," << fieldIdx << "]\n";
    return b->CreateGEP(structTy, ptrToStruct, indeces, "sa_" + std::to_string(fieldIdx) + annot);
}

Value *makeFlator(Value *original, IRBuilder<> *b, StructMap *structMap,
                  std::map<Value *, Value *> *writebackQ, bool isInflate, Value *newStruct = NULL) {

    LLVMContext *C = &b->getContext();
    if (original->getType()->isStructTy()) {
        /* this case is extremely complicated */
        assert(false);
    } else if (original->getType()->isArrayTy()) {
        /* code */
        assert(false);
    } else if (original->getType()->isPointerTy() &&
               original->getType()->getPointerElementType()->isStructTy()) {
        errs() << "inserting " << (isInflate ? "in" : "de") << "flator for " << *original << "\n";
        StructType *ogType = dyn_cast<StructType>(original->getType()->getPointerElementType());
        StructInfo *info = structMap->at(ogType).get();
        StructType *newType = isInflate ? info->inflatedType : info->deflatedType;

        assert(ogType);
        assert(isInflate ? ogType->elements().size() < newType->elements().size()
                         : newType->elements().size() < ogType->elements().size());

        newStruct = newStruct ? newStruct : b->CreateAlloca(newType, 0, "newStruct");
        assert(newStruct->getType()->isPointerTy());
        assert(newStruct->getType()->getPointerElementType() == newType);

        int i = 0;
        auto it = isInflate ? ogType->element_begin() : newType->element_begin();
        auto end = isInflate ? ogType->element_end() : newType->element_end();

        for (; it != end; it++) {
            Value *ptrToOgField = makeStructAccessGEP(
                b, ogType, original, (isInflate ? i : info->offsetMapping.at(i)), "_og");
            Value *ptrToNewField = makeStructAccessGEP(
                b, newType, newStruct, (isInflate ? info->offsetMapping.at(i) : i), "_new");
            i++;
            Value *sizeofField = createSizeof(b, *it);
            CallInst *memcpy = b->CreateMemCpy(ptrToNewField, MaybeAlign(), ptrToOgField,
                                               MaybeAlign(), sizeofField);
        }
        if (writebackQ) {
            writebackQ->insert({newStruct, original});
        }

        return newStruct;
    } else {
        errs() << "Value does not have to be copied\n";
        return original;
    }
}

/***
 * @param isInflate means here that we'd be starting with inflated arguments,
 * converting those to deflated arguments, calling and inflating them once again.
 * In other words: we are inflating the original function.
 */
void createFlationWrapper(StructMap *structMap, LLVMContext *C, Function *wrapper,
                          Function *originalFunc, bool isInflate) {
    errs() << "creating " << (isInflate ? "in" : "de") << "flation for " << originalFunc->getName()
           << " at " << wrapper->getName() << "\n";
    IRBuilder<> b(*C);
    BasicBlock *entryBB = BasicBlock::Create(*C, // create a new body in this function
                                             "ENTRY", wrapper);
    b.SetInsertPoint(entryBB);

    SmallVector<Value *> newArgs;
    std::map<Value *, Value *> structsToWriteBack;
    for (Argument &ogArg : wrapper->args()) {
        PointerType *ptrTy = dyn_cast<PointerType>(ogArg.getType());
        if (ptrTy && ptrTy->getPointerElementType()->isStructTy()) {
            newArgs.push_back(makeFlator(&ogArg, &b, structMap, &structsToWriteBack, !isInflate));
        } else if (ogArg.getType()->isStructTy()) {
            assert(false);
        } else {
            newArgs.push_back(&ogArg);
        }
    }
    CallInst *ogRetVal = b.CreateCall(originalFunc, newArgs);

    // TODO: write back here
    for (auto [from, to] : structsToWriteBack) {
        makeFlator(from, &b, structMap, NULL, isInflate, to);
    }

    if (originalFunc->getReturnType() != Type::getVoidTy(*C)) {
        Value *inflRetVal = makeFlator(ogRetVal, &b, structMap, &structsToWriteBack, isInflate);
        b.CreateRet(inflRetVal);
    } else {
        b.CreateRet(NULL);
    }
}

void populate_delicate_functions(StructMap *structMap, LLVMContext *C) {
    /**
     * for every function in the library_call_queue:
     *  * make a dci wrapper (deflate -> call -> inflate)
     * for every function in the exported_func_queue
     *  * make a icd wrapper(inflate -> call -> deflate)
     */

    IRBuilder<> builder(*C);
    for (std::pair<Function *, Function *> pair : libraryFuncsToWrap) {
        Function *inflatedFunc = pair.first;
        Function *originalFunc = pair.second;
        createFlationWrapper(structMap, C, inflatedFunc, originalFunc, true);
    }
    for (std::pair<Function *, Function *> pair : exportedFuncsToWrap) {
        Function *inflatedFunc = pair.first;
        Function *originalFunc = pair.second;
        // Note: a bit hacky, but this ensures there is in fact an entry point called 'main'.
        // alternative (probably more clean) solution is to not inflate main. But what we cannot do
        // is create deflator wrappers around all functions; this messes with function pointers.
        if (originalFunc->getName() == "main") {
            createFlationWrapper(structMap, C, originalFunc, inflatedFunc, false);
        }
    }
}
