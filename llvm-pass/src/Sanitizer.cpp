#include <map>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "redzone.h"

using namespace llvm;

namespace {
	// The size of redzones, in bytes.
	const size_t REDZONE_SIZE = 1;
	
	// Forward declaration to be able to use it in field info.
	struct StructInfo;
	
	struct FieldInfo {
		// If the field this info represents is a struct type, will contain the StructInfo for this.
		// Otherwise, will be NULL.
		std::shared_ptr<StructInfo> structInfo;
		// The size (in bytes) of the field.
		// In case of a struct, this is usually slightly more than the actual struct size due to alignment.
		size_t size;
	};
	struct StructInfo {
    	// The llvm struct type.
    	StructType* type;
    	// The modified struct type that contains redzones.
    	StructType* inflatedType;
    	// The fields present in the struct.
    	std::vector<FieldInfo> fields;
    	// The total size of the struct. Usually slightly more than the summation of the sizes of all fields due to alignment.
    	size_t size;
    	// A mapping from offsets in the unmapped type into the mapped type.
    	std::map<size_t, size_t> offsetMapping;
    };
    
	struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
		// Walks over a struct to provide information on its fields.
		// If this struct contains another struct, will recurse.
		std::shared_ptr<StructInfo> WalkStruct(StructType *s, DataLayout &dl, LLVMContext &ctx)
		{
			// Metadata info on each field of the struct
			std::vector<FieldInfo> fields;
			// The fields, but with redzone types inserted in between them.
			std::vector<Type*> mappedFields;
			
			std::map<size_t, size_t> offset_mapping;
			
			size_t base_offset = 0;
			for (auto fieldType : s->elements())
			{
				FieldInfo field = {
					nullptr,
					dl.getTypeAllocSize(fieldType),
				};
				if (auto* structType = dyn_cast<StructType>(fieldType))
				{
					field.structInfo = WalkStruct(structType, dl, ctx);
				}
				fields.push_back(field);
				// Here, we push both the original field type, and an array of chars with a given size to store the redzone in.
				mappedFields.push_back(fieldType);
				mappedFields.push_back(ArrayType::get(Type::getInt8Ty(ctx), REDZONE_SIZE));
				// Finally, we store the difference in offsets that accumulates due to redzones.
				offset_mapping[base_offset] = base_offset * 2;
				base_offset += 1;
			}
			// The last redzone (at least for now) is superfluous since it does not protect against internal overflows.
			// when we add external overflow guards, we can simply remove this and add one redzone before we loop over the elements.
			mappedFields.pop_back();
			
			// If the inflated type doesn't exist yet, create it. For recursion, it will already exist, so lets not create duplicates.
			auto inflated_type = StructType::getTypeByName(ctx, s->getName().str() + ".inflated");
			if (inflated_type == NULL)
			{
				inflated_type = StructType::create(ctx, s->getName().str() + ".inflated");
				inflated_type->setBody(ArrayRef<Type*>(mappedFields));
			}
			
			struct StructInfo si = {
				s,
				inflated_type,
				fields,
				dl.getTypeAllocSize(s),
				offset_mapping,
			};
			return std::make_shared<StructInfo>(si);
		}
	
		PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
			test_func();
			auto datalayout = M.getDataLayout();
			auto &context = M.getContext();
			// Iterate over all struct types. I believe this one does NOT yet deal with external struct definitions (header files even, perhaps? certainly not libraries)
			std::map<Type*, std::shared_ptr<StructInfo>> struct_mapping;
			for (auto st : M.getIdentifiedStructTypes())
			{
				auto si = WalkStruct(st, datalayout, context);
				struct_mapping[st] = si;
			}
			// Printing to verify everything works as expected. just for debugging.
			for (const auto& [st, structInfo] : struct_mapping)
			{
				int fieldCount = 0;
				structInfo->type->dump();
				outs() << "Has inflated variant:\n";
				structInfo->inflatedType->dump();
				outs() << "And fields:\n";
				for (auto& fieldInfo: structInfo->fields)
				{
					if (fieldInfo.structInfo)
					{
						int inner_fieldCount = 0;
						outs() << "\tNested struct type " << fieldCount << ":" << fieldInfo.size << "\n";
						fieldInfo.structInfo->type->dump();
						outs() << "\tHas inflated variant:\n";
						fieldInfo.structInfo->inflatedType->dump();
						outs() << "\tAnd fields:\n";
						for (auto& inner_fieldInfo: fieldInfo.structInfo->fields)
						{
							outs() << "\t\t\t" << inner_fieldCount << ":" << inner_fieldInfo.size << "\n";
							inner_fieldCount += 1;
						}
					}
					else 
					{
						outs() << "\t" << fieldCount << ":" << fieldInfo.size << "\n";
					}
					fieldCount += 1;
				}
				outs() << "Offset mapping:\n";
				for (const auto& [key, value] : structInfo->offsetMapping)
				{
						outs() << key << ": " << value << '\n';
				}
			}
			for (auto &func : M) {
				// Then it is an external function, and must be linked.
				// We can't instrument this - though it is probably interesting in a later stage for inflating/deflating structs.
				if (func.isDeclaration())
				{
					continue;
				}
				// Here, we store instructions and the replacements they will get.
				// This construction is necessary because doing so invalidates existing iterators, so we cannot do it inside the loop.
				// Additionally, note that it is not really feasible to directly store the replacing instructions, as they have to already be inserted to exist.
				// Thus, we store the components we need to construct them.
				std::map<Instruction*, Type*> alloca_replacements;
				std::map<GetElementPtrInst*, std::tuple<Type *, Type *, std::vector<Value*>>> gep_replacements;

				for (auto &bb : func) {
					for (auto &inst : bb) {
						if (auto *gep_inst = dyn_cast<GetElementPtrInst>(&inst))
						{
							auto src_type = gep_inst->getSourceElementType();
							// NOTE: the result element type will probably be relevant when we add support for nested structs.
							auto dest_type = gep_inst->getResultElementType();
							// In this case, we know the GEP refers to a struct type that will be inflated, so we should update its offset (and type).
							if (struct_mapping.count(src_type) > 0)
							{
								if (struct_mapping.count(dest_type) > 0)
								{
									dest_type = struct_mapping[dest_type]->inflatedType;
									// TODO on nested structs: deal with that.
									outs() << "NOTE: ";
									inst.dump();
									outs() << "is a gep for a nested type: ";
									dest_type->dump();
									outs() << "\n";
								}
								std::vector<Value*> replaced_indices;
								int cnt = 0;
								for (auto &idx: gep_inst->indices())
								{
									if (auto *const_int = dyn_cast<ConstantInt>(idx))
									{
										// For a singular struct access, the first index is always zero, whereas the second index is the field index.
										if (cnt == 1)
										{
											replaced_indices.push_back(ConstantInt::get(context, const_int->getValue() * 2));
										}
										else
										{
											replaced_indices.push_back(const_int);
										}
									}
									else {
										outs() << "ERROR: unknown index type at:";
										gep_inst->print(outs());
										outs() << " - ";
										idx->print(outs());
										outs() << "\n";
										abort();
									}
									cnt += 1;
								}
								gep_replacements[gep_inst] = std::make_tuple(
									struct_mapping[src_type]->inflatedType,
									dest_type,
									replaced_indices
								);
							}
						}
						else if (auto *alloca_inst = dyn_cast<AllocaInst>(&inst))
						{
							auto alloc_type = alloca_inst->getAllocatedType();
							// If this is the case, we know the struct type and can inflate it.
							if (struct_mapping.count(alloc_type) > 0)
							{
								alloca_replacements[alloca_inst] = struct_mapping[alloc_type]->inflatedType;
							}
							else if (alloc_type->isArrayTy())
							{
								outs() << "No support yet for arrays of structs. If this is an array of structs, it will be ignored.\n";
							}
							else if (alloc_type->isStructTy()) {
								errs() << "Error: unknown struct detected:\n";
								alloc_type->dump();
								abort();
							}
						}
					}
				}
				IRBuilder<> builder(context);
				for (const auto& [inst, new_type] : alloca_replacements)
				{
					// NOTE: to support array types, we will need to pass it something else than nullptr. so we can extend what is stored in alloca_replacements to deal with that.
					builder.SetInsertPoint(inst);
					auto *new_alloca_inst = builder.CreateAlloca(new_type, nullptr);
					inst->replaceAllUsesWith(new_alloca_inst);
					inst->eraseFromParent();
				}
				for (const auto& [inst, tup] : gep_replacements)
				{
					// NOTE: we _cannot_ move this to the other loop, because this gets altered by the alloca instruction replacements!
					auto *ptr = inst->getPointerOperand();
					builder.SetInsertPoint(inst);
					// TODO on nested structs: somehow properly coerce the result element type?
					// or is the issue present because we might be altering some instructions before the instructions they depend on are changed?
					// possible solution is to first deal with all instructions that do _not_ have this, and then with those that do.
					auto *newInst = builder.CreateGEP(
						  std::get<0>(tup),
						  ptr,
						  ArrayRef<Value*>(std::get<2>(tup))
					);
					
					inst->replaceAllUsesWith(newInst);
		  			inst->eraseFromParent();
				}
			}
			// NOTE: what redzone type do we want to use? i.e., what way do we check if one is hit?
		  // TODO: from within the pass:
			// 3. investigate what instructions are practically used to access the structs. (load, store?)
			// 4. add sanitation checks there, for _internal overflow_.
			// (i.e., whenever a load happens whose source is a getelementptr associated to a struct instance, first insert a call to a function which crashes if the memory is a redzone)
			// 5. verify the program now crashes on internal overflow, but not on the safe variant.
			// 6. move on to nested structs. then after, external overflows.
		  return PreservedAnalyses::all();
		}
	};
}


// register to pass manager
extern "C" LLVM_ATTRIBUTE_WEAK llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "StructZoneSanitizer", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &PM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "structzone-sanitizer") {
                    PM.addPass(StructZoneSanitizer());
                    return true;
                  }
                  return false;
                });
          }};
}

