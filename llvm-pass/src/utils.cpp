
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <map>

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "utils.h"

Value* createSizeof(IRBuilder<>* builder, Type* accessedType){
    SmallVector<Value *> one = {
        ConstantInt::get(IntegerType::getInt8Ty(builder->getContext()), APInt(8, 1))};
    ConstantPointerNull *typedNullPtr = ConstantPointerNull::get(PointerType::get(accessedType, 0));
    Value *sizeofPtr = builder->CreateGEP(accessedType, typedNullPtr, one);
    Value *sizeofInt = builder->CreatePtrToInt(sizeofPtr, IntegerType::getInt8Ty(builder->getContext()));
    return sizeofInt;
}