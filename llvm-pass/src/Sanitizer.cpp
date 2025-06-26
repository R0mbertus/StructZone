#include <fstream>
#include <map>
#include <stack>

#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "functionTransformer.h"
#include "redzone.h"

using namespace llvm;

namespace {
// typedefs for the long types we use in the pass.
typedef std::vector<std::function<void(LLVMContext &)>> UpdateInstMap;

struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
    // mapping from old struct to new struct
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

    // Performs a _shallow_ walk over all the fields of the struct.
    // In particular, this means pointer types will not be updated, to avoid infinite recursion.
    std::shared_ptr<StructInfo> ShallowWalk(StructType *s, DataLayout &dl, LLVMContext &ctx) {
        // Metadata info on each field of the struct
        std::vector<FieldInfo> fields;
        // The fields, but with redzone types inserted in between them.
        std::vector<Type *> mappedFields;
        std::vector<size_t> redzone_offsets;
        std::map<size_t, size_t> offset_mapping;
        // We add a redzone before the first field, so that we can detect underflows.
        // However, if the type is opaque (i.e., a forward declaration), we should not touch it.
        if (!s->isOpaque()) {
            redzone_offsets.push_back(mappedFields.size());
            mappedFields.push_back(ArrayType::get(Type::getInt8Ty(ctx), REDZONE_SIZE));
        }
        size_t base_offset = 0;
        for (auto fieldType : s->elements()) {
            FieldInfo field = {
                fieldType, nullptr,
                fieldType->isSized() ? dl.getTypeAllocSize(fieldType)
                                     : 0ul, // Let us hope 0 is a reasonable default
            };
            std::stack<size_t> nestingInfo;
            auto *currType = fieldType;
            do {
                // Note that we explicitly avoid walking over pointer types here, to make sure the
                // walk is shallow.
                if (auto *arrayType = dyn_cast<ArrayType>(currType)) {
                    nestingInfo.push(arrayType->getNumElements());
                    currType = arrayType->getElementType();
                } else if (!currType->isStructTy()) {
                    // Not a known type that can contain other types within, so just end the loop
                    currType = nullptr;
                }
            } while (currType && !currType->isStructTy());

            if (!currType) {
                mappedFields.push_back(fieldType);
            } else {
                // Now, we can recurse over the struct type we found.
                auto innerStructInfo = ShallowWalk(dyn_cast<StructType>(currType), dl, ctx);
                // TODO: while this correctly indicates that somewhere inside the type, there is an
                // inflated struct, this doesn't tell us where. we should probably move the
                // unpacking/repacking to a separate method, or otherwise store the information
                // somewhere.
                field.structInfo = innerStructInfo;
                // And then, we need to re-create the types.
                Type *inflatedInnerType = innerStructInfo->inflatedType;
                while (nestingInfo.size() > 0) {
                    auto size = nestingInfo.top();
                    nestingInfo.pop();
                    inflatedInnerType = ArrayType::get(inflatedInnerType, size);
                }
                mappedFields.push_back(inflatedInnerType);
            }
            fields.push_back(field);
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
            if (mappedFields.size() > 0) {
                inflated_type->setBody(ArrayRef<Type *>(mappedFields));
            }
        }

        struct StructInfo si = {s, inflated_type, s, fields,
                                // For an opaque struct, we of course cannot infer the size.
                                s->isSized() ? dl.getTypeAllocSize(s) : 0ul,
                                inflated_type->isSized() ? dl.getTypeAllocSize(inflated_type) : 0ul,
                                offset_mapping, redzone_offsets};
        return std::make_shared<StructInfo>(si);
    }

    // Performs a deep walk on all struct types that are already shallowly inflated,
    // by also considering pointers (and thus, self-referential types).
    void DeepWalk(DataLayout &dl, LLVMContext &ctx) {
        for (const auto &[type, si] : struct_mapping) {
            if (si->type->isOpaque()) {
                continue;
            }
            std::vector<Type *> mappedFields;
            mappedFields.push_back(ArrayType::get(Type::getInt8Ty(ctx), REDZONE_SIZE));
            int idx = 0;
            for (auto fieldType : si->type->elements()) {
                std::stack<std::tuple<bool, size_t>> nestingInfo;
                auto *currType = fieldType;
                do {
                    if (auto *arrayType = dyn_cast<ArrayType>(currType)) {
                        nestingInfo.push(std::make_tuple(true, arrayType->getNumElements()));
                        currType = arrayType->getElementType();
                    } else if (auto *ptrType = dyn_cast<PointerType>(currType)) {
                        nestingInfo.push(std::make_tuple(false, 0));
                        currType = ptrType->getPointerElementType();
                    } else if (!currType->isStructTy()) {
                        // Not a known type that can contain other types within, so just end the
                        // loop
                        currType = nullptr;
                    }
                } while (currType && !currType->isStructTy());

                if (!currType) {
                    mappedFields.push_back(fieldType);
                } else {
                    // Now, we can look up the struct we found
                    auto innerStructInfo = struct_mapping[currType];
                    // Update it here, in case we missed it the first time around
                    si->fields[idx].structInfo = innerStructInfo;
                    // And then, we need to re-create the types.
                    Type *inflatedInnerType = innerStructInfo->inflatedType;
                    while (nestingInfo.size() > 0) {
                        auto tup = nestingInfo.top();
                        nestingInfo.pop();
                        if (std::get<0>(tup)) {
                            inflatedInnerType = ArrayType::get(inflatedInnerType, std::get<1>(tup));
                        } else {
                            inflatedInnerType =
                                PointerType::get(inflatedInnerType, 0); // default address space
                        }
                    }
                    mappedFields.push_back(inflatedInnerType);
                }
                // and an array of chars with a given size to store the redzone in.
                mappedFields.push_back(ArrayType::get(Type::getInt8Ty(ctx), REDZONE_SIZE));
                idx += 1;
            }
            // update the body of the inflated type again, to correct pointer types.
            si->inflatedType->setBody(ArrayRef<Type *>(mappedFields));
        }
    }

    void update_inst_gep(GetElementPtrInst *inst, Type *type, std::vector<Value *> value,
                         LLVMContext &context) {
        IRBuilder<> builder(context);
        builder.SetInsertPoint(inst);
        auto *newInst =
            builder.CreateGEP(type,
                              // NOTE: we _cannot_ move this to the other loop, because this
                              // gets altered by the alloca instruction replacements!
                              inst->getPointerOperand(), value);
        inst->replaceAllUsesWith(newInst);
        inst->eraseFromParent();
    }

    void update_inst_alloca(Instruction *inst, Type *type, Value *value, LLVMContext &context) {
        IRBuilder<> builder(context);
        builder.SetInsertPoint(inst);
        // Note: the second element will be null for non-arrays.
        auto *newInst = builder.CreateAlloca(type, value);
        inst->replaceAllUsesWith(newInst);
        inst->eraseFromParent();
    }

    void update_inst_bitcast(BitCastInst *inst, Type *type, LLVMContext &context) {
        IRBuilder<> builder(context);
        builder.SetInsertPoint(inst);
        auto *newInst = builder.CreateBitCast(inst->getOperand(0), type);
        inst->replaceAllUsesWith(newInst);
        inst->eraseFromParent();
    }

    void update_inst_load(LoadInst *inst, Type *type, LLVMContext &context) {
        IRBuilder<> builder(context);
        builder.SetInsertPoint(inst);
        auto *newInst = builder.CreateLoad(type, inst->getPointerOperand());
        inst->replaceAllUsesWith(newInst);
        inst->eraseFromParent();
    }

    void update_inst_call(CallInst *inst, LLVMContext &context) {
        IRBuilder<> builder(context);
        builder.SetInsertPoint(inst);
        std::vector<Value *> args;
        for (int i = 0; i < inst->arg_size(); i++) {
            args.push_back(inst->getArgOperand(i));
        }
        auto *calledVal = inst->getCalledOperand();
        if (auto *func_ptr = dyn_cast<PointerType>(calledVal->getType())) {
            auto *new_call = builder.CreateCall(
                dyn_cast<FunctionType>(func_ptr->getPointerElementType()), calledVal, args);
            inst->replaceAllUsesWith(new_call);
            inst->eraseFromParent();
        }
    }
    std::map<PHINode *, std::tuple<Instruction *, Type *>> to_resolve;
    void update_inst_phi(PHINode *phi_inst, Type *type, LLVMContext &context) {
        IRBuilder<> builder(context);
        // before the first non phi instruction. this is because if we have multiple phi nodes, they
        // _all_ have to appear before any other instructions.
        builder.SetInsertPoint(phi_inst->getParent()->getFirstNonPHI());
        // we create a bitcast right after the phi instruction. this allows us to break any possible
        // cyclic dependencies.
        auto *bitcast = builder.CreateBitCast(phi_inst, type);
        // effectively identical to replaceAllUsesWith, except that we don't consider the bitcast.
        std::vector<Use *> phi_uses;
        for (auto &use : phi_inst->uses()) {
            if (use.getUser() != bitcast) {
                phi_uses.push_back(&use);
            }
        }
        for (auto *use : phi_uses) {
            use->set(bitcast);
        }
        to_resolve[phi_inst] = std::make_tuple(dyn_cast<Instruction>(bitcast), type);
    }

    void handle_gep(GetElementPtrInst *gep_inst, UpdateInstMap &update_insts,
                    LLVMContext &context) {
        auto src_type = gep_inst->getSourceElementType();
        int count = 0;
        std::vector<Value *> replaced_indices;
        Type *curr_type = src_type->getPointerTo();
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
                    errs() << "ERROR: unknown index type at:";
                    gep_inst->print(errs());
                    errs() << " - ";
                    idx->print(errs());
                    errs() << "\n";
                    abort();
                }
            }
            // We should not encounter unknown type kinds here.
            // If we do, that means we cannot complete the walk over the indices, so we error out.
            else {
                errs() << "Unknown type ";
                curr_type->print(errs());
                errs() << ". This implies either an unknown type _kind_, or an uninflated struct "
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
        bool hasChanged = false;
        auto *inflatedType = getInflatedType(src_type, &hasChanged);
        if (hasChanged) {
            // we are now done reasoning about the element type, so turn it back into an array type
            // if that is what we started with.
            if (arr_type) {
                inflatedType = ArrayType::get(inflatedType, arr_type->getArrayNumElements());
            }
            update_insts.push_back(
                [this, gep_inst, inflatedType, replaced_indices](LLVMContext &context) {
                    update_inst_gep(gep_inst, inflatedType, replaced_indices, context);
                });
        }
    }

    void handle_alloca(AllocaInst *alloca_inst, UpdateInstMap &update_insts, LLVMContext &context,
                       DataLayout &datalayout) {
        auto alloc_type = alloca_inst->getAllocatedType();
        errs() << "ALLOCA: \n";
        alloca_inst->dump();
        errs() << "TYPE: \n";
        alloc_type->dump();
        // If this is the case, we know the struct type and can inflate it.
        if (struct_mapping.count(alloc_type) > 0) {
            StructType *struct_type = struct_mapping[alloc_type]->inflatedType;
            update_insts.push_back([this, alloca_inst, struct_type](LLVMContext &context) {
                update_inst_alloca(alloca_inst, struct_type, nullptr, context);
            });
        } else if (auto *alloc_arr_type = dyn_cast<ArrayType>(alloc_type)) {
            // Here, we have an array of structs.
            if (struct_mapping.count(alloc_arr_type->getElementType()) > 0) {
                // So we need to inflate the _element_ type.
                auto *new_arr_type =
                    ArrayType::get(struct_mapping[alloc_arr_type->getElementType()]->inflatedType,
                                   alloc_arr_type->getNumElements());
                auto *array_size = alloca_inst->getArraySize();
                update_insts.push_back(
                    [this, alloca_inst, new_arr_type, array_size](LLVMContext &context) {
                        update_inst_alloca(alloca_inst, new_arr_type, array_size, context);
                    });
            } else {
                AssertNonStructType(alloc_arr_type->getElementType());
            }
        } else if (auto *ptr_type = dyn_cast<PointerType>(alloc_type)) {
            bool hasChanged = false;
            errs() << "FIRST TEST: \n";
            ptr_type->dump();
            auto *inflatedType = getInflatedType(ptr_type, &hasChanged);
            if (!hasChanged) {
                return;
            }
			errs() << "TEST: ";
			inflatedType->print(errs());
			outs() << "\nFor: ";
			alloca_inst->print(errs());
			errs() << "\n";
            // Replace pointer to struct with pointer to inflated struct.
            update_insts.push_back([this, alloca_inst, inflatedType](LLVMContext &context) {
                update_inst_alloca(alloca_inst, inflatedType, nullptr, context);
            });
        } else {
            AssertNonStructType(alloc_type);
        }
    }

    void handle_bitcast(BitCastInst *bitcast_inst, UpdateInstMap &update_insts,
                        LLVMContext &context,
                        std::map<CallInst *, std::tuple<StructInfo, size_t>> *heapStructInfo) {
        // Replace all uses of other insts will take care of src type already.
        if (auto *dest_type = dyn_cast<PointerType>(bitcast_inst->getDestTy())) {
            bool hasChanged = false;
            auto *inflated_dest_type = getInflatedType(dest_type, &hasChanged);
            if (!hasChanged) {
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
                        errs() << "ALLOCATION TEST: \n";
                        if (!dest_type->getPointerElementType()->isStructTy()) {
                        	return; // pointers and the like
                        }
                        dest_type->dump();
                        auto struct_info = struct_mapping[dest_type->getPointerElementType()];
                        auto count = const_int->getValue().udiv(struct_info->size);
                        auto *new_size = ConstantInt::get(call_inst->getArgOperand(i)->getType(),
                                                          struct_info->inflatedSize * count);
                        call_inst->setArgOperand(i, new_size);
                        heapStructInfo->insert(
                            {call_inst, std::make_tuple(*struct_info.get(), count.getZExtValue())});

                    } else {
                        // Note: if we hit this, then (m/re/c)alloc are getting a non-constant size
                        // parameter. i dont expect this to happen, but if it does, it should error
                        // out for now so we can ensure its sane to add support for it.
                        errs() << "ERROR: unknown size type at:";
                        call_inst->print(errs());
                        errs() << " - ";
                        call_inst->getArgOperand(i)->print(errs());
                        errs() << "\n";
                        return;
                        abort();
                    }
                };

                if (call_inst->getCalledFunction()->getName().equals("malloc.inflated")) {
                    // If malloc, we need to update the first argument to the inflated size.
                    update_size(0);
                } else if (call_inst->getCalledFunction()->getName().equals("calloc.inflated") ||
                           call_inst->getCalledFunction()->getName().equals("realloc.inflated")) {
                    // If calloc or realloc, we need to update the second argument to the inflated
                    // size.
                    update_size(1);
                }
            }

            // Then, we can replace the type with the inflated type.
            update_insts.push_back([this, bitcast_inst, inflated_dest_type](LLVMContext &context) {
                this->update_inst_bitcast(bitcast_inst, inflated_dest_type, context);
            });
        }
    }

    void handle_load(LoadInst *load_inst, UpdateInstMap &update_insts, LLVMContext &context) {
        bool hasChanged = false;
        auto *inflatedType = getInflatedType(load_inst->getType(), &hasChanged);
        // No struct type present; no need to update.
        if (!hasChanged) {
            return;
        }
        update_insts.push_back([this, load_inst, inflatedType](LLVMContext &context) {
            this->update_inst_load(load_inst, inflatedType, context);
        });
    }

    void handle_phi(PHINode *phi_node, UpdateInstMap &update_insts, LLVMContext &context) {
        bool hasChanged = false;
        auto *inflatedType = getInflatedType(phi_node->getType(), &hasChanged);
        // No struct type present; no need to update.
        if (!hasChanged) {
            return;
        }
        update_insts.push_back([this, phi_node, inflatedType](LLVMContext &context) {
            this->update_inst_phi(phi_node, inflatedType, context);
        });
    }

    void handle_call(CallInst *call_inst, UpdateInstMap &update_insts, LLVMContext &context) {
        auto *calledVal = call_inst->getCalledOperand();
        if (!isa<Function>(calledVal)) {
            update_insts.push_back([this, call_inst](LLVMContext &context) {
                this->update_inst_call(call_inst, context);
            });
        } else {
            // This is a horifically specific bit of code that deals with function
            // pointers, because of the way llvm chooses to bitcast them when they
            // are created/passed in a call.
            for (int i = 0; i < call_inst->arg_size(); i++) {
                auto *current_arg = call_inst->getArgOperand(i);
                if (auto *constExpr = dyn_cast<ConstantExpr>(current_arg)) {
                    if (constExpr->getOpcode() == Instruction::BitCast) {
                        if (auto *func_type = dyn_cast<FunctionType>(
                                constExpr->getType()->getPointerElementType())) {
                            SmallVector<Type *> newParams;
                            for (auto *p = func_type->param_begin(); p < func_type->param_end();
                                 p++) {
                                auto *newParam = getInflatedType(*p, NULL);
                                newParams.push_back(newParam);
                            }
                            FunctionType *inflatedFuncType =
                                FunctionType::get(getInflatedType(func_type->getReturnType(), NULL),
                                                  newParams, func_type->isVarArg());
                            auto *funcPtrType = PointerType::get(inflatedFuncType, 0);
                            auto *newBitCast =
                                ConstantExpr::getBitCast(constExpr->getOperand(0), funcPtrType);
                            call_inst->setArgOperand(i, newBitCast);
                        }
                    }
                }
            }
        }
    }

    Type *getInflatedType(Type *arg_type, bool *changed = NULL) {
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
            arg_type_cp = struct_mapping[arg_type_cp].get()->inflatedType;
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
                    if (auto *callInst = dyn_cast<CallInst>(&inst)) {
                        if (callInst->getCalledFunction() == oldFunc) {
                            callInst->setCalledFunction(newFunc);
                        }
                    }
                }
            }
        }
    }

    // this function is mostly here to make sure the module is still valid after
    // transformation. It converts any struct args to inflated args
    void replaceFunctionTypes(Function *func) {
        /**
         * For all args:
         *  * get the type
         *  * if pointer recurse
         */
        bool hasStructArgs = false;
        SmallVector<Type *> newArgs;
        ValueToValueMapTy map;
        errs() << "checking function " << func->getName();

        for (Argument *a = func->arg_begin(); a < func->arg_end(); a++) {
            Type *newArg = getInflatedType(a->getType(), &hasStructArgs);
            newArgs.push_back(newArg);
        }
        FunctionType *inflatedFuncType = FunctionType::get(
            getInflatedType(func->getReturnType(), &hasStructArgs), newArgs, func->isVarArg());

        if (!hasStructArgs) {
            errs() << " (skipped, no struct args/ret-value)\n";
            return;
        }

        Function *newFunc = Function::Create(inflatedFuncType, Function::InternalLinkage,
                                             func->getName() + ".inflated", func->getParent());

        for (size_t i = 0; i < newFunc->arg_size(); i++) {
            map.insert({func->getArg(i), newFunc->getArg(i)});
        }
        SmallVector<ReturnInst *> returns;
        errs() << " cloning " << func->getName() << " to " << newFunc->getName() << "\n";
        rebuildCalls(func->getParent(), func, newFunc);
        CloneFunctionInto(newFunc, func, map, CloneFunctionChangeType::LocalChangesOnly, returns);
        func->replaceAllUsesWith(newFunc);
        func->deleteBody();
    }

    void save_mod(Module *M) {
        std::error_code EC;
        raw_fd_ostream out("./last_executed_module.ll", EC, sys::fs::OF_Text);
        if (EC) {
            errs() << "Error opening file " << EC.message() << "\n";
        }
        M->print(out, nullptr);
    }

    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
        auto datalayout = M.getDataLayout();
        auto &context = M.getContext();
        std::map<CallInst *, std::tuple<StructInfo, size_t>> heapStructInfo;

        // Iterate over all struct types. I believe this one does NOT yet deal with external struct
        // definitions (header files even, perhaps? certainly not libraries)
        for (auto st : M.getIdentifiedStructTypes()) {
            auto si = ShallowWalk(st, datalayout, context);
            struct_mapping[si.get()->inflatedType] = si;
            struct_mapping[si.get()->deflatedType] = si;
        }
        DeepWalk(datalayout, context);

        SmallVector<Function *> funcs;
        for (auto &func : M) {
            funcs.push_back(&func);
        }
        for (Function *func : funcs) {
            transformFuncSig(func, &struct_mapping);
        }
        for (auto &func : M) {
        	outs() << "Started resolving function: " << func.getName() << "\n";
        	to_resolve.clear();
            // Then it is an external function, and must be linked. We can't instrument this -
            // though it is probably interesting in a later stage for inflating/deflating structs.
            if (func.isDeclaration()) {
            	outs() << "Finished function: " << func.getName() << "\n";
                continue;
            }
            // Here, we store instructions and the replacements they will get. This construction is
            // necessary because doing so invalidates existing iterators, so we cannot do it inside
            // the loop. Additionally, note that it is not really feasible to directly store the
            // replacing instructions, as they have to already be inserted to exist.
            // Thus, we store a lambda which can be called to do the updating.
            UpdateInstMap update_insts;

            for (auto &bb : func) {
                for (auto &inst : bb) {
                    if (auto *gep_inst = dyn_cast<GetElementPtrInst>(&inst)) {
                        handle_gep(gep_inst, update_insts, context);
                    } else if (auto *alloca_inst = dyn_cast<AllocaInst>(&inst)) {
                        handle_alloca(alloca_inst, update_insts, context, datalayout);
                    } else if (auto *bitcast_inst = dyn_cast<BitCastInst>(&inst)) {
                        handle_bitcast(bitcast_inst, update_insts, context, &heapStructInfo);
                    } else if (auto *load_inst = dyn_cast<LoadInst>(&inst)) {
                        handle_load(load_inst, update_insts, context);
                    } else if (auto *phi_inst = dyn_cast<PHINode>(&inst)) {
                        handle_phi(phi_inst, update_insts, context);
                    } else if (auto *call_inst = dyn_cast<CallInst>(&inst)) {
                        handle_call(call_inst, update_insts, context);
                    }
                }
                save_mod(&M);
            }

            for (auto &update_inst : update_insts) {
                update_inst(context);
            }
            // Phi nodes are a special case; we break their cyclic dependency with a bitcast.
            // to be safe, we can then delay updating phi nodes until the very end (because all
            // dependencies will be updated at that point).
            save_mod(&M);
            IRBuilder<> builder(context);
            for (auto &[phi_inst, tup] : to_resolve) {
                auto *phi_type = std::get<1>(tup);
                auto *bitcast = std::get<0>(tup);
                errs() << "PHI: ";
                phi_inst->dump();
                errs() << "NEW TYPE: ";
                phi_type->dump();
                builder.SetInsertPoint(phi_inst);
                auto *new_phi = builder.CreatePHI(phi_type, phi_inst->getNumIncomingValues());
                for (unsigned i = 0; i < phi_inst->getNumIncomingValues(); ++i) {
                    new_phi->addIncoming(phi_inst->getIncomingValue(i),
                                         phi_inst->getIncomingBlock(i));
                }
                phi_inst->replaceAllUsesWith(new_phi);
                phi_inst->eraseFromParent();
                bitcast->replaceAllUsesWith(new_phi);
                bitcast->eraseFromParent();
                save_mod(&M);
            }
            outs() << "Finished function: " << func.getName() << "\n";
            save_mod(&M);
        }
        // Some more TODO's:
        // see if we can move to storing marker values in redzones, and only walking the tree if
        // we detect a marker value (but what about unaligned reads?)

        setupRedzoneChecks(&struct_mapping, M, &heapStructInfo);
        populate_delicate_functions(&struct_mapping, &M.getContext());
        save_mod(&M);

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
