#include "redzone.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include <stdio.h>

/**
 * TODO:
 *  Think of a fast datastructure to quickly find weather a memory area is poisoned
 *  insert checks on loads
 */

void add_runtime_linkage(Module &M){
    SmallVector<Type*> arguments = {};
    FunctionType *f = FunctionType::get(IntegerType::get(M.getContext(), 32), 
        ArrayRef<Type*>(arguments), false);
    
    FunctionCallee prototype = M.getOrInsertFunction("test_runtime_link", f);
}

void initialise_alloca_struct(AllocaInst* allocaInst){
    
}

void setup_redzone_initialiser(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M){
    add_runtime_linkage(M);
    for (Function &func : M){
        for(BasicBlock &bb : func){
            for(Instruction &inst : bb){
                if (AllocaInst* allocaInst = dyn_cast<AllocaInst>(&inst))
                {
                    initialise_alloca_struct(allocaInst);
                }
            }
        }
    }
}

void setup_redzone_checks(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M){
    
}