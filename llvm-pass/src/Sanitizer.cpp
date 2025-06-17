#include <map>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"


#include "redzone.h"

using namespace llvm;

namespace {
// typedefs for the long types we use in the pass.
typedef std::map<GetElementPtrInst *, std::tuple<Type *, std::vector<Value *>>> GepMap;
typedef std::map<Instruction *, std::tuple<Type *, Value *>> AllocaMap;
typedef std::map<BitCastInst *, Type *> BitcastMap;
typedef std::map<LoadInst *, Type *> LoadMap;

struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
    std::map<Type *, std::shared_ptr<StructInfo>> struct_mapping;

    // Helper function to deduplicate the sanity checks.
    // Typically used when we want to verify all struct types are capable of being inflated.
    void AssertNonStructType(Type *type) {
        if (type->isStructTy()) {
            errs() << "Error: unknown struct detected:\n";
            type->print(errs());
            errs() << "\n";
            abort();
        }
    }
    // Walks over a struct to provide information on its fields.
    // If this struct contains another struct, will recurse.
    std::shared_ptr<StructInfo> WalkStruct(StructType *s, DataLayout &dl, LLVMContext &ctx) {
        // Metadata info on each field of the struct
        std::vector<FieldInfo> fields;
        // The fields, but with redzone types inserted in between them.
        std::vector<Type *> mappedFields;
        std::vector<size_t> redzone_offsets;
        std::map<size_t, size_t> offset_mapping;
        // We add a redzone before the first field, so that we can detect underflows.
        redzone_offsets.push_back(mappedFields.size());
        mappedFields.push_back(ArrayType::get(Type::getInt8Ty(ctx), REDZONE_SIZE));
        size_t base_offset = 0;
        for (auto fieldType : s->elements()) {
            FieldInfo field = {
                fieldType,
                nullptr,
                dl.getTypeAllocSize(fieldType),
            };
            if (auto *structType = dyn_cast<StructType>(fieldType)) {
                field.structInfo = WalkStruct(structType, dl, ctx);
            }
            fields.push_back(field);
            // Here, we push either the original field type, _or_ the inflated variant if we are
            // dealing with nested structs.
            if (field.structInfo) {
                mappedFields.push_back(field.structInfo->inflatedType);
            } else {
                mappedFields.push_back(fieldType);
            }
            // and an array of chars with a given size to store the redzone in.
            redzone_offsets.push_back(mappedFields.size());
            mappedFields.push_back(ArrayType::get(Type::getInt8Ty(ctx), REDZONE_SIZE));
            // Finally, we store the difference in offsets that accumulates due to redzones.
            offset_mapping[base_offset] = base_offset * 2 + 1;
            base_offset += 1;
        }
        // If the inflated type doesn't exist yet, create it. For recursion, it will already exist,
        // so lets not create duplicates.
        auto inflated_type = StructType::getTypeByName(ctx, s->getName().str() + ".inflated");
        if (inflated_type == NULL) {
            inflated_type = StructType::create(ctx, s->getName().str() + ".inflated");
            inflated_type->setBody(ArrayRef<Type *>(mappedFields));
        }

        struct StructInfo si = {s,
                                inflated_type,
                                fields,
                                dl.getTypeAllocSize(s),
                                dl.getTypeAllocSize(inflated_type),
                                offset_mapping,
                                redzone_offsets};
        return std::make_shared<StructInfo>(si);
    }

    void handle_gep(GetElementPtrInst *gep_inst, GepMap &gep_replacements, LLVMContext &context) {
        auto src_type = gep_inst->getSourceElementType();
        int count = 0;
        std::vector<Value *> replaced_indices;
        Type* curr_type = src_type->getPointerTo(); 
        // We walk over all indices, keeping track of the current type. This allows us to detect all
        // shapes of struct access that might require offset changing.
        for (auto &idx : gep_inst->indices()) {
            // Arrays do not require index replacement, and the next index will refer to the element
            // type.
            if (auto *arr_type = dyn_cast<ArrayType>(curr_type)) {
                replaced_indices.push_back(idx);
                curr_type = arr_type->getElementType();
            }
            // Pointers do not require index replacement, and the next index will refer to the type
            // that is pointed to.
            else if (auto *ptr_type = dyn_cast<PointerType>(curr_type)) {
                replaced_indices.push_back(idx);
                curr_type = ptr_type->getPointerElementType();
            }
            // Struct types require index replacement. Additionally, the next type is the type of
            // the field that is pointed to.
            else if (struct_mapping.count(curr_type) > 0) {
                if (auto *const_int = dyn_cast<ConstantInt>(idx)) {
                    replaced_indices.push_back(
                        ConstantInt::get(context, const_int->getValue() * 2 + 1));
                    auto si = struct_mapping[curr_type];
                    // Assuming zero extension is fine, because a negative field index is not
                    // semantically correct.
                    curr_type = si->fields.at(const_int->getValue().getZExtValue()).type;
                } else {
                    // This one is conceptually... weird. A non-constant index really only makes
                    // sense on something like an array, which has a homogeneous element type. But
                    // if it is a struct, you no longer have that. Therefore, I assume this
                    // should never happen.
                    outs() << "ERROR: unknown index type at:";
                    gep_inst->print(outs());
                    outs() << " - ";
                    idx->print(outs());
                    outs() << "\n";
                    abort();
                }
            }
            // We should not encounter unknown type kinds here.
            // If we do, that means we cannot complete the walk over the indices, so we error out.
            else {
                outs() << "Unknown type ";
                curr_type->print(outs());
                outs() << ". This implies either an unknown type _kind_, or an uninflated struct "
                          "type.\n";
                abort();
            }
            count += 1;
        }
        
        ArrayType *arr_type = nullptr;
        // Array type? no problem. we can just reason over the element type!
        if (auto *src_arr_type = dyn_cast<ArrayType>(src_type)) {
            arr_type = src_arr_type;
            src_type = src_arr_type->getElementType();
        }

        // In this case, we know the GEP refers to a struct type that will be inflated, so we should
        // update its offset (and type).
        if (struct_mapping.count(src_type) > 0) {
            Type *res_type = nullptr;
            // we are now done reasoning about the element type, so turn it back into an array type
            // if that is what we started with.
            if (arr_type) {
                res_type = ArrayType::get(struct_mapping[src_type]->inflatedType,
                                          arr_type->getArrayNumElements());
            } else {
                res_type = struct_mapping[src_type]->inflatedType;
            }
            gep_replacements[gep_inst] = std::make_tuple(res_type, replaced_indices);
        } else {
            AssertNonStructType(src_type);
        }
    }

    void handle_alloca(AllocaInst *alloca_inst, AllocaMap &alloca_replacements,
                       LLVMContext &context, DataLayout &datalayout) {
        auto alloc_type = alloca_inst->getAllocatedType();
        // If this is the case, we know the struct type and can inflate it.
        if (struct_mapping.count(alloc_type) > 0) {
            alloca_replacements[alloca_inst] =
                std::make_tuple(struct_mapping[alloc_type]->inflatedType, nullptr);
        } else if (auto *alloc_arr_type = dyn_cast<ArrayType>(alloc_type)) {
            // Here, we have an array of structs.
            if (struct_mapping.count(alloc_arr_type->getElementType()) > 0) {
                // So we need to inflate the _element_ type.
                auto *new_arr_type =
                    ArrayType::get(struct_mapping[alloc_arr_type->getElementType()]->inflatedType,
                                   alloc_arr_type->getNumElements());
                alloca_replacements[alloca_inst] =
                    std::make_tuple(new_arr_type, alloca_inst->getArraySize());
            } else {
                AssertNonStructType(alloc_arr_type->getElementType());
            }
        } else if (auto *ptr_type = dyn_cast<PointerType>(alloc_type)) {
            if (struct_mapping.count(ptr_type->getPointerElementType()) == 0) {
                AssertNonStructType(ptr_type->getPointerElementType());
                return;
            }

            // Replace pointer to struct with pointer to inflated struct.
            auto *new_alloca_type = PointerType::getUnqual(
                struct_mapping[ptr_type->getPointerElementType()]->inflatedType);
            alloca_replacements[alloca_inst] = std::make_tuple(new_alloca_type, nullptr);
        } else {
            AssertNonStructType(alloc_type);
        }
    }

    void handle_bitcast(BitCastInst *bitcast_inst, BitcastMap &bitcast_replacements,
                        LLVMContext &context, std::map<CallInst*, struct StructInfo>* heapStructInfo) {
        // Replace all uses of other insts will take care of src type already.
        if (auto *dest_type = dyn_cast<PointerType>(bitcast_inst->getDestTy())) {
            if (struct_mapping.count(dest_type->getPointerElementType()) == 0) {
                AssertNonStructType(dest_type->getPointerElementType());
                return;
            }

            // First, if src is a pointer, look up if that src is from a malloc.
            auto *call_inst = dyn_cast<CallInst>(bitcast_inst->getOperand(0));
            if (isa<PointerType>(bitcast_inst->getSrcTy()) && call_inst &&
                call_inst->getCalledFunction()) {
                // Lambda as the code below is practically identical for all three cases.
                auto update_size = [&](size_t i) {
                    if (auto *const_int = dyn_cast<ConstantInt>(call_inst->getArgOperand(i))) {
                        // Take the old malloc size, divide it by the original struct size, and
                        // multiply it by the inflated struct size to handle arrays of structs.
                        auto struct_info = struct_mapping[dest_type->getPointerElementType()];
                        auto count = const_int->getValue().udiv(struct_info->size);
                        auto *new_size = ConstantInt::get(call_inst->getArgOperand(i)->getType(),
                                                          struct_info->inflatedSize * count);
                        call_inst->setArgOperand(i, new_size);
                        heapStructInfo->insert({call_inst, *struct_info.get()});
                        
                    } else {
                        // Note: if we hit this, then (m/re/c)alloc are getting a non-constant size
                        // parameter. i dont expect this to happen, but if it does, it should error
                        // out for now so we can ensure its sane to add support for it.
                        outs() << "ERROR: unknown size type at:";
                        call_inst->print(outs());
                        outs() << " - ";
                        call_inst->getArgOperand(i)->print(outs());
                        outs() << "\n";
                        abort();
                    }
                };

                if (call_inst->getCalledFunction()->getName().equals("malloc")) {
                    // If malloc, we need to update the first argument to the inflated size.
                    update_size(0);
                } else if (call_inst->getCalledFunction()->getName().equals("calloc") ||
                           call_inst->getCalledFunction()->getName().equals("realloc")) {
                    // If calloc or realloc, we need to update the second argument to the inflated
                    // size.
                    update_size(1);
                }
            }

            // Then, we can replace the type with the inflated type.
            bitcast_replacements[bitcast_inst] = PointerType::getUnqual(
                struct_mapping[dest_type->getPointerElementType()]->inflatedType);
        }
    }

    void handle_load(LoadInst *load_inst, LoadMap &load_replacements, LLVMContext &context) {
        auto *load_type = load_inst->getType();
        if (auto *load_type_ptr = dyn_cast<PointerType>(load_type)) {
            if (struct_mapping.count(load_type_ptr->getPointerElementType()) == 0) {
                AssertNonStructType(load_type_ptr->getPointerElementType());
                return;
            }

            // Replace with pointer to inflated type.
            load_replacements[load_inst] = PointerType::getUnqual(
                struct_mapping[load_type_ptr->getPointerElementType()]->inflatedType);
        }
    }

    Type* getInflatedType(Type* arg_type, bool* changed = NULL){
        int pointer_layers = 0;
        Type* arg_type_cp = arg_type;
        while (arg_type_cp->isPointerTy())
        {
            pointer_layers++;
            assert(!arg_type_cp->isOpaquePointerTy());
            arg_type_cp = arg_type_cp->getPointerElementType();
        }
        if (arg_type_cp->isStructTy())
        {
            if(changed != NULL){
                *changed = true;
            }
            arg_type_cp = struct_mapping[arg_type_cp].get()->inflatedType;
        }
        for (int i = 0; i < pointer_layers; i++)
        {
            arg_type_cp = arg_type_cp->getPointerTo();
        }
        return arg_type_cp;
    }

    void rebuildCalls(Module* M, Function* oldFunc, Function* newFunc){
        for(Function &F : *M){
            for (BasicBlock& bb : F)
            {
                for (Instruction& inst : bb)
                {
                    CallInst* callInst = dyn_cast<CallInst>(&inst);
                    if(callInst){
                        if (callInst->getCalledFunction() == oldFunc)
                        {
                            callInst->setCalledFunction(newFunc);
                        }
                        
                    }
                }
            }
        }
    }

    //this function is mostly here to make sure the module is still valid after
    //transformation. It converts any struct args to inflated args
    void replaceFunctionTypes(Function *func){
        /**
         * For all args:
         *  * get the type
         *  * if pointer recurse
         */
        bool hasStructArgs = false;
        SmallVector<Type*> newArgs;
        ValueToValueMapTy map;
        outs() << "checking function " << func->getName();

        for (Argument* a = func->arg_begin(); a < func->arg_end(); a++)
        {
            Type* newArg = getInflatedType(a->getType(), &hasStructArgs);
            newArgs.push_back(newArg);
        }
        FunctionType* inflatedFuncType = FunctionType::get(
            getInflatedType(func->getReturnType(), &hasStructArgs), newArgs, func->isVarArg()
        );

        if(!hasStructArgs){
            outs() << " (skipped, no struct args/ret-value)\n";
            return;
        }

        Function* newFunc = Function::Create(inflatedFuncType, 
            Function::InternalLinkage, func->getName() + ".inflated", func->getParent());
        
        for (size_t i = 0; i < newFunc->arg_size(); i++)
        {
            map.insert({func->getArg(i), newFunc->getArg(i)});
        }
        SmallVector<ReturnInst*> returns;
        outs() << " cloning " << func->getName() << " to " << newFunc->getName() << "\n";
        rebuildCalls(func->getParent(), func, newFunc);
        CloneFunctionInto(newFunc, func, map, CloneFunctionChangeType::LocalChangesOnly, returns);
        func->replaceAllUsesWith(newFunc);
        func->deleteBody();
        
    }

    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
        auto datalayout = M.getDataLayout();
        auto &context = M.getContext();
        std::map<CallInst*, StructInfo> heapStructInfo;

        // Iterate over all struct types. I believe this one does NOT yet deal with external struct
        // definitions (header files even, perhaps? certainly not libraries)
        for (auto st : M.getIdentifiedStructTypes()) {
            auto si = WalkStruct(st, datalayout, context);
            struct_mapping[st] = si;
        }

        SmallVector<Function*> funcs;
        for (auto &func : M)
        {
            funcs.push_back(&func);
        }
        for (Function* func : funcs)
        {
            replaceFunctionTypes(func);
        }
        

        for (auto &func : M) {
            // Then it is an external function, and must be linked. We can't instrument this -
            // though it is probably interesting in a later stage for inflating/deflating structs.
            if (func.isDeclaration()) {
                continue;
            }
            // Here, we store instructions and the replacements they will get. This construction is
            // necessary because doing so invalidates existing iterators, so we cannot do it inside
            // the loop. Additionally, note that it is not really feasible to directly store the
            // replacing instructions, as they have to already be inserted to exist. Thus, we store
            // the components we need to construct them.
            AllocaMap alloca_replacements;
            GepMap gep_replacements;
            BitcastMap bitcast_replacements;
            LoadMap load_replacements;

            for (auto &bb : func) {
                for (auto &inst : bb) {
                    if (auto *gep_inst = dyn_cast<GetElementPtrInst>(&inst)) {
                        handle_gep(gep_inst, gep_replacements, context);
                    } else if (auto *alloca_inst = dyn_cast<AllocaInst>(&inst)) {
                        handle_alloca(alloca_inst, alloca_replacements, context, datalayout);
                    } else if (auto *bitcast_instr = dyn_cast<BitCastInst>(&inst)) {
                        handle_bitcast(bitcast_instr, bitcast_replacements, context, &heapStructInfo);
                    } else if (auto *load_instr = dyn_cast<LoadInst>(&inst)) {
                        handle_load(load_instr, load_replacements, context);
                    }
                }
            }

            IRBuilder<> builder(context);
            for (const auto &[inst, tup] : alloca_replacements) {
                builder.SetInsertPoint(inst);
                // Note: the second element will be null for non-arrays.
                auto *newInst = builder.CreateAlloca(std::get<0>(tup), std::get<1>(tup));
                inst->replaceAllUsesWith(newInst);
                inst->eraseFromParent();
            }

            for (const auto &[inst, type] : bitcast_replacements) {
                builder.SetInsertPoint(inst);
                auto *newInst = builder.CreateBitCast(inst->getOperand(0), type);
                inst->replaceAllUsesWith(newInst);
                inst->eraseFromParent();
            }

            for (const auto &[inst, type] : load_replacements) {
                builder.SetInsertPoint(inst);
                auto *newInst = builder.CreateLoad(type, inst->getPointerOperand());
                inst->replaceAllUsesWith(newInst);
                inst->eraseFromParent();
            }

            for (const auto &[inst, tup] : gep_replacements) {
                builder.SetInsertPoint(inst);
                auto *newInst = builder.CreateGEP(
                    std::get<0>(tup),
                    // NOTE: we _cannot_ move this to the other loop, because this gets altered by
                    // the alloca instruction replacements!
                    inst->getPointerOperand(),
                    // NOTE 2: some internal llvm magic is happening with arrayref; doing it in the
                    // prior loop gives strange failures.
                    ArrayRef<Value *>(std::get<1>(tup)));
                inst->replaceAllUsesWith(newInst);
                inst->eraseFromParent();
            }
        }
        // Some more TODO's:
        // 1. add redzones for heap struct,
        // 2. add redzones for nested struct types,
        // 3. add redzones for arrays of structs,
        // 4. look into makefile shenanigans to see why IR isn't being outputted/why runtime changes aren't detected for tests
        // 5. see if we can move to storing marker values in redzones, and only walking the tree if we detect a marker value
        // (but what about unaligned reads?)

        setupRedzoneChecks(&struct_mapping, M, &heapStructInfo);
        outs() << "done!\n";
        return PreservedAnalyses::none();
    }
};
} // namespace

// register to pass manager
extern "C" LLVM_ATTRIBUTE_WEAK llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "StructZoneSanitizer", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback([](StringRef Name, ModulePassManager &PM,
                                                      ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "structzone-sanitizer") {
                        PM.addPass(StructZoneSanitizer());
                        return true;
                    }
                    return false;
                });
            }};
}
