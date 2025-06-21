#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <cstdarg>


#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "functionTransformer.h"
#include "redzone.h"
#include "utils.h"

Type *getInflatedType(Type *arg_type, std::map<Type *, std::shared_ptr<StructInfo>> *struct_mapping,
                      bool *changed = NULL) {
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
    }
    for (int i = 0; i < pointer_layers; i++) {
        arg_type_cp = arg_type_cp->getPointerTo();
    }
    return arg_type_cp;
}

void rebuildCalls(Module *M, Function *oldFunc, Function *newFunc) {
    for (Function &F : *M) {
        for (BasicBlock &bb : F) {
            for (Instruction &inst : bb) {
                CallInst *callInst = dyn_cast<CallInst>(&inst);
                if (callInst) {
                    if (callInst->getCalledFunction() == oldFunc) {
                        callInst->setCalledFunction(newFunc);
                    }
                }
            }
        }
    }
}

void addDeflationToLibFunc(Function *func) {
    // this is a function called by us
}

void addDeflationToExternal(Function *func) {}

// this function is mostly here to make sure the module is still valid after
// transformation. It converts any struct args to inflated args

Function *createInflatedEmpty(Function *original,
                              std::map<Type *, std::shared_ptr<StructInfo>> *struct_mapping,
                              bool *hasStructArgs) {
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

Function *makeInflatedClone(Function *original,
                            std::map<Type *, std::shared_ptr<StructInfo>> *struct_mapping) {
    bool hasStructArgs;
    ValueToValueMapTy map;

    Function *newFunc = createInflatedEmpty(original, struct_mapping, &hasStructArgs);
    for (size_t i = 0; i < newFunc->arg_size(); i++) {
        map.insert({original->getArg(i), newFunc->getArg(i)});
    }
    outs() << "exported func: " << original->getName() << " has a twin: " << newFunc->getName() << "\n";
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

Function *makeInflatedWrapper(Function *original,
                              std::map<Type *, std::shared_ptr<StructInfo>> *struct_mapping) {
    bool hasStructArgs;
    Function *newFunc = createInflatedEmpty(original, struct_mapping, &hasStructArgs);
    outs() << "library func: " << original->getName() << " has a twin " << newFunc->getName() << "\n";
    libraryFuncsToWrap.insert({newFunc, original});

    return newFunc;
}

void transformFuncSig(Function *original,
                      std::map<Type *, std::shared_ptr<StructInfo>> *struct_mapping) {
    Function *replacement = NULL;
    if (original->isIntrinsic())
    {
        return;
    }
    else if (original->isDeclaration()) {
        replacement = makeInflatedWrapper(original, struct_mapping);
    } else {
        replacement = makeInflatedClone(original, struct_mapping);
    }
}

Value* makeStructAccessGEP(IRBuilder<>* b, StructType* structToAccess, Value* ptrToStruct, int fieldIdx){
    LLVMContext* C = &b->getContext();
    SmallVector<Value*> indeces = {
        ConstantInt::get(IntegerType::getInt32Ty(*C), 0),
        ConstantInt::get(IntegerType::getInt32Ty(*C), fieldIdx)
    };
    outs() << "  GEP " <<  structToAccess->getName() << " " << *ptrToStruct << " [0," << fieldIdx << "]\n";
    return b->CreateGEP(structToAccess, ptrToStruct, indeces, "");
}

Value *makeDI(Value *deflatedType, IRBuilder<>* builder, std::map<Type*,std::shared_ptr<StructInfo>> *struct_mapping) {
    /**
     * If structype:
     *   Allocate a struct
     *   copy some fields
     *   send it
     * If pointer to struct:
     *   Allocate new struct
     *   copy some fields
     *   send it
     * If pointer to pointer to struct:
     *   allocate a pointer
     *   deref the pointer,
     */
    LLVMContext* C = &builder->getContext();
    if (deflatedType->getType()->isStructTy())
    {
        /* this case is extremely complicated */
        assert(false);
    } 
    else if (deflatedType->getType()->isArrayTy())
    {
        /* code */
        assert(false);
    }
    else if (deflatedType->getType()->isPointerTy() && 
    deflatedType->getType()->getPointerElementType()->isStructTy())
    {
        outs() << "inserting inflator for " << *deflatedType << "\n";
        StructType* deflType = dyn_cast<StructType>(deflatedType->getType()->getPointerElementType());
        assert(deflType);
        StructInfo* info = struct_mapping->at(deflType).get();
        Value* inflatedStruct = builder->CreateAlloca(info->inflatedType, 0, "");
        StructType* inflType = info->inflatedType;
        
        int i = 0;
        for (Type* field : deflType->elements())
        {
            Value* ptrToDeflField = makeStructAccessGEP(builder, deflType, deflatedType, i);
            Value* ptrToInflField = makeStructAccessGEP(builder, inflType, inflatedStruct, i);
            info->offsetMapping.at(i);
            i++; 
            Value* sizeofField = createSizeof(builder, field);
            CallInst* memcpy = builder->CreateMemCpy(ptrToInflField, MaybeAlign(),  
                ptrToDeflField, MaybeAlign(), sizeofField);
        }
        return inflatedStruct;
    } else {
        outs() << "Value does not have to be copied\n";
        return deflatedType;
    }
}

Value *makeID(Value *inflatedType, IRBuilder<>* builder, std::map<Type*,std::shared_ptr<StructInfo>> *struct_mapping) {
    LLVMContext* C = &builder->getContext();
    if (inflatedType->getType()->isStructTy())
    {
        /* this case is complicated */
        assert(false);
    } 
    else if (inflatedType->getType()->isArrayTy())
    {
        /* code */
        assert(false);
    }
    else if (inflatedType->getType()->isPointerTy() && 
    inflatedType->getType()->getPointerElementType()->isStructTy()){
        outs() << "inserting deflator for " << *inflatedType << "\n";
        StructType* inflType = dyn_cast<StructType>(inflatedType->getType()->getPointerElementType());
        assert(inflType);
        StructInfo* info = struct_mapping->at(inflType).get();
        Value* deflatedStruct = builder->CreateAlloca(info->deflatedType, 0, "");
        outs() << "  <-- " << *deflatedStruct << "\n";
        StructType* deflType = info->deflatedType;
        
        int i = 0;
        for(Type* field : info->deflatedType->elements()){
            Value* ptrToDeflField = makeStructAccessGEP(builder, deflType, deflatedStruct, i);
            Value* ptrToInflField = makeStructAccessGEP(builder, inflType, inflatedType, i);
            info->offsetMapping.at(i);
            i++; 
            Value* sizeofField = createSizeof(builder, field);
            CallInst* memcpy = builder->CreateMemCpy(ptrToInflField, MaybeAlign(),  
                ptrToDeflField, MaybeAlign(), sizeofField);
        }
        return deflatedStruct;
    }
    else{
        return inflatedType;
    }
}

void populate_delicate_functions(std::map<Type*,std::shared_ptr<StructInfo>> *defl2info, LLVMContext *C) {
    /**
     * for every function in the library_call_queue:
     *  * make a dci wrapper (deflate -> call -> inflate)
     * for every function in the exported_func_queue
     *  * make a icd wrapper(inflate -> call -> deflate)
     */

    IRBuilder<> builder(*C);
    for (std::pair<Function *, Function *> pair : libraryFuncsToWrap) {
        Function* inflatedFunc = pair.first;
        Function* originalFunc = pair.second;

        BasicBlock* entryBB = BasicBlock::Create(*C, //create a new body in this function
            "ENTRY", inflatedFunc);
        builder.SetInsertPoint(entryBB);
        
        SmallVector<Value*> deflatedArgs;
        for (Argument &infArg : inflatedFunc->args())
        {
            deflatedArgs.push_back(makeID(&infArg, &builder, defl2info));
        }
        CallInst* deflRetVal = builder.CreateCall(originalFunc, deflatedArgs);
        
        if (originalFunc->getReturnType() != Type::getVoidTy(*C))
        {
            Value* inflRetVal = makeDI(deflRetVal, &builder, defl2info);
            builder.CreateRet(inflRetVal);
        } else{
            builder.CreateRet(NULL);
        }
    }
    for (std::pair<Function*, Function*> pair : exportedFuncsToWrap){
        Function* inflatedFunc = pair.first;
        Function* originalFunc = pair.second;
        
        BasicBlock* entryBB = BasicBlock::Create(*C, 
            "ENTRY", originalFunc);
        builder.SetInsertPoint(entryBB);

        SmallVector<Value*> inflatedArgs;
        for (Argument &infArg : originalFunc->args())
        {
            inflatedArgs.push_back(makeDI(&infArg, &builder, defl2info));
        }
        CallInst* inflRetVal = builder.CreateCall(inflatedFunc, inflatedArgs);
        
        if (originalFunc->getReturnType() != Type::getVoidTy(*C))
        {
            Value* deflRetVal = makeID(inflRetVal, &builder, defl2info);
            builder.CreateRet(deflRetVal);
        } else{
            builder.CreateRet(NULL);
        }
    }
}