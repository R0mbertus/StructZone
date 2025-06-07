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
    FunctionType *funcType = FunctionType::get(Type::getVoidTy(M.getContext()), 
        ArrayRef<Type*>(arguments), false);
    
    // FunctionCallee prototype = M.getOrInsertFunction("test_runtime_link", f);
    Function* externalFunc = Function::Create(funcType, Function::ExternalLinkage, "test_runtime_link", M);

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
                CallInst *newCall = CallInst::Create(externalFunc, "", &I);
                outs() << "call : " << *newCall << "\n";
                break;
            }
            break;
        }
        break;
    }
    // outs() << M;
}

void initialise_alloca_struct(AllocaInst* allocaInst){
    
}

void setup_redzone_initialiser(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M){
    add_runtime_linkage(M);
    // for (Function &func : M){
    //     for(BasicBlock &bb : func){
    //         for(Instruction &inst : bb){
    //             if (AllocaInst* allocaInst = dyn_cast<AllocaInst>(&inst))
    //             {
    //                 initialise_alloca_struct(allocaInst);
    //             }
    //         }
    //     }
    // }
}

void setup_redzone_checks(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M){
    setup_redzone_initialiser(info, M);
}