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

Type *getInflatedType(Type *arg_type, StructMap *struct_mapping,
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
                              StructMap *struct_mapping,
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
                            StructMap *struct_mapping) {
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
                              StructMap *struct_mapping) {
    bool hasStructArgs;
    Function *newFunc = createInflatedEmpty(original, struct_mapping, &hasStructArgs);
    outs() << "library func: " << original->getName() << " has a twin " << newFunc->getName() << "\n";
    libraryFuncsToWrap.insert({newFunc, original});

    return newFunc;
}

void transformFuncSig(Function *original,
                      StructMap *struct_mapping) {
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

Value* makeStructAccessGEP(IRBuilder<>* b, StructType* structTy, 
Value* ptrToStruct, int fieldIdx, std::string annot=""){
    LLVMContext* C = &b->getContext();
    SmallVector<Value*> indeces = {
        ConstantInt::get(IntegerType::getInt32Ty(*C), 0),
        ConstantInt::get(IntegerType::getInt32Ty(*C), fieldIdx)
    };
    outs() << "  GEP " <<  structTy->getName() << " " << *ptrToStruct << " [0," << fieldIdx << "]\n";
    return b->CreateGEP(structTy, ptrToStruct, indeces, "sa_" + std::to_string(fieldIdx) + annot);
}

Value* makeFlator(Value* original, IRBuilder<>* b, StructMap* structMap, 
std::map<Value*,Value*>* writebackQ, bool isInflate, Value* newStruct = NULL){
    
    LLVMContext* C = &b->getContext();
    if (original->getType()->isStructTy())
    {
        /* this case is extremely complicated */
        assert(false);
    } 
    else if (original->getType()->isArrayTy())
    {
        /* code */
        assert(false);
    }
    else if (original->getType()->isPointerTy() && 
    original->getType()->getPointerElementType()->isStructTy())
    {
        outs() << "inserting " << (isInflate? "in" : "de") << "flator for " << *original << "\n";
        StructType* ogType = dyn_cast<StructType>(original->getType()->getPointerElementType());
        StructInfo* info = structMap->at(ogType).get();
        StructType* newType = isInflate? info->inflatedType : info->deflatedType;
        
        assert(ogType);
        assert(isInflate? ogType->elements().size() < newType->elements().size() :
            newType->elements().size() < ogType->elements().size());
        
        newStruct = newStruct? newStruct : b->CreateAlloca(newType, 0, "newStruct");
        assert(newStruct->getType()->isPointerTy());
        assert(newStruct->getType()->getPointerElementType() == newType);
        
        int i = 0;
        auto it = isInflate? ogType->element_begin() : newType->element_begin();
        auto end = isInflate? ogType->element_end() : newType->element_end();
        
        for (; it != end; it++)
        {
            Value* ptrToOgField = makeStructAccessGEP(b, ogType, 
                original, (isInflate? i : info->offsetMapping.at(i)), "_og");
            Value* ptrToNewField = makeStructAccessGEP(b, newType,
                newStruct, (isInflate? info->offsetMapping.at(i) : i), "_new");
            i++; 
            Value* sizeofField = createSizeof(b, *it);
            CallInst* memcpy = b->CreateMemCpy(ptrToNewField, MaybeAlign(),  
                ptrToOgField, MaybeAlign(), sizeofField);
        }
        if (writebackQ)
        {
            writebackQ->insert({newStruct, original});
        }

        return newStruct;
    } else {
        outs() << "Value does not have to be copied\n";
        return original;
    }
}

/***
 * @param isInflate means here that we'd be starting with inflated arguments,
 * converting those to deflated arguments, calling and inflating them once again.
 * In other words: we are inflating the original function.
 */
void createFlationWrapper(StructMap *structMap, LLVMContext *C, 
Function* wrapper, Function* originalFunc, bool isInflate){ 
    outs() << "creating " << (isInflate? "in" : "de") << "flation for " 
        << originalFunc->getName() << " at " << wrapper->getName() << "\n";
    IRBuilder<> b(*C);
    BasicBlock* entryBB = BasicBlock::Create(*C, //create a new body in this function
        "ENTRY", wrapper);
    b.SetInsertPoint(entryBB);
    
    SmallVector<Value*> newArgs;
    std::map<Value*,Value*> structsToWriteBack;
    for (Argument &ogArg : wrapper->args())
    {
        PointerType* ptrTy = dyn_cast<PointerType>(ogArg.getType());
        if (ptrTy && ptrTy->getPointerElementType()->isStructTy())
        {
            newArgs.push_back(makeFlator(&ogArg, &b, structMap, &structsToWriteBack, !isInflate));
        }
        else if (ogArg.getType()->isStructTy())
        {
            assert(false);
        }
        else{
            newArgs.push_back(&ogArg);
        }
    }
    CallInst* ogRetVal = b.CreateCall(originalFunc, newArgs);
    
    //TODO: write back here
    for (auto [from, to] : structsToWriteBack)
    {
        makeFlator(from, &b, structMap, NULL, isInflate, to);
    }

    if (originalFunc->getReturnType() != Type::getVoidTy(*C))
    {
        Value* inflRetVal = makeFlator(ogRetVal, &b, structMap, &structsToWriteBack, isInflate);
        b.CreateRet(inflRetVal);
    } else{
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
        Function* inflatedFunc = pair.first;
        Function* originalFunc = pair.second;
        createFlationWrapper(structMap, C, inflatedFunc, originalFunc, true);
    }
    for (std::pair<Function*, Function*> pair : exportedFuncsToWrap){
        Function* inflatedFunc = pair.first;
        Function* originalFunc = pair.second;
        createFlationWrapper(structMap, C, originalFunc, inflatedFunc, false);
    }
}