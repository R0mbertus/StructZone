#include <map>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "redzone.h"

using namespace llvm;

namespace {
	// typedefs for the long types we use in the pass.
	typedef std::map<GetElementPtrInst*, std::tuple<Type *, std::vector<Value*>>> GepMap;
	typedef std::map<Instruction*, std::tuple<Type*, Value*>> AllocaMap;
	typedef std::map<BitCastInst*, Type*> BitcastMap;
	typedef std::map<LoadInst*, Type*> LoadMap;
	
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
    	// The total size of the _inflated_ struct.
    	size_t inflatedSize;
    	// A mapping from offsets in the unmapped type into the mapped type.
    	std::map<size_t, size_t> offsetMapping;
    };
    
	struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
		std::map<Type*, std::shared_ptr<StructInfo>> struct_mapping;
		
		
		// Helper function to deduplicate the sanity checks.
		// Typically used when we want to verify all struct types are capable of being inflated.
		void AssertNonStructType(Type* type)
		{
			if (type->isStructTy())
			{
				errs() << "Error: unknown struct detected:\n";
				type->print(errs());
				errs() << "\n";
				abort();
			}
		}
		
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
				// Here, we push either the original field type, _or_ the inflated variant if we are dealing with nested structs.
				if (field.structInfo)
				{
					mappedFields.push_back(field.structInfo->inflatedType);
				}
				else
				{	
					mappedFields.push_back(fieldType);
				}
				// and an array of chars with a given size to store the redzone in.
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
				dl.getTypeAllocSize(inflated_type),
				offset_mapping,
			};
			return std::make_shared<StructInfo>(si);
		}

		void handle_gep(GetElementPtrInst *gep_inst, GepMap &gep_replacements, LLVMContext &context) {
			auto src_type = gep_inst->getSourceElementType();
			ArrayType* arr_type = nullptr;
			// Array type? no problem. we can just reason over the element type!
			if (auto *src_arr_type = dyn_cast<ArrayType>(src_type))
			{
				arr_type = src_arr_type;
				if (src_arr_type->getElementType()->isStructTy())
				{
					src_type = src_arr_type->getElementType();
				}
			}
			
			// In this case, we know the GEP refers to a struct type that will be inflated, so we should update its offset (and type).
			if (struct_mapping.count(src_type) > 0)
			{
				std::vector<Value*> replaced_indices;
				int cnt = 0;
				for (auto &idx: gep_inst->indices())
				{
					if (arr_type)
					{
						//Then, we can just push back the index immediately, because we don't insert redzones between array elements.
						replaced_indices.push_back(idx);
					}
					else if (auto *const_int = dyn_cast<ConstantInt>(idx))
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
						// TODO: in heap arrays of structs, we get sign extensions on a gep's second index, and it is seen as an array type because the source type is Simple*.
						// so we need to distinguish those cases.
						// NOTE: if we practically hit this, it requires runtime multiplication of two. not very clean.. but it should work.
						outs() << "ERROR: unknown index type at:";
						gep_inst->print(outs());
						outs() << " - ";
						idx->print(outs());
						outs() << "\n";
						abort();
					}
					cnt += 1;
				}
				Type* res_type = nullptr;
				// we are now done reasoning about the element type, so turn it back into an array type if that is what we started with.
				if (arr_type)
				{
					res_type = ArrayType::get(struct_mapping[src_type]->inflatedType, arr_type->getArrayNumElements());
				}
				else 
				{
					res_type = struct_mapping[src_type]->inflatedType;
				}
				gep_replacements[gep_inst] = std::make_tuple(
					res_type,
					replaced_indices
				);
			}
			else
			{
				AssertNonStructType(src_type);
			}
		}

		void handle_alloca(AllocaInst *alloca_inst, AllocaMap &alloca_replacements, LLVMContext &context, DataLayout &datalayout) {
			auto alloc_type = alloca_inst->getAllocatedType();
			// If this is the case, we know the struct type and can inflate it.
			if (struct_mapping.count(alloc_type) > 0)
			{
				alloca_replacements[alloca_inst] = std::make_tuple(struct_mapping[alloc_type]->inflatedType, nullptr);
			}
			else if (auto *alloc_arr_type = dyn_cast<ArrayType>(alloc_type))
			{
				// Here, we have an array of structs.
				if (struct_mapping.count(alloc_arr_type->getElementType()) > 0)
				{
					// So we need to inflate the _element_ type.
					auto* new_arr_type = ArrayType::get(
						struct_mapping[alloc_arr_type->getElementType()]->inflatedType,
						alloc_arr_type->getNumElements()
					);
					alloca_replacements[alloca_inst] = std::make_tuple(new_arr_type, alloca_inst->getArraySize());
				}
				else 
				{
					AssertNonStructType(alloc_arr_type->getElementType());
				}
			}
			else if (auto *ptr_type = dyn_cast<PointerType>(alloc_type))
			{
				if (struct_mapping.count(ptr_type->getPointerElementType()) == 0)
				{
					AssertNonStructType(ptr_type->getPointerElementType());
					return;
				}

				// Replace pointer to struct with pointer to inflated struct.
				auto *new_alloca_type = PointerType::getUnqual(
					struct_mapping[ptr_type->getPointerElementType()]->inflatedType);
				alloca_replacements[alloca_inst] = std::make_tuple(new_alloca_type, nullptr);
			}
			else 
			{
				AssertNonStructType(alloc_type);
			}
		}

		void handle_bitcast(BitCastInst *bitcast_inst, BitcastMap &bitcast_replacements, LLVMContext &context) {
			// Replace all uses of other insts will take care of src type already.
			if (auto *dest_type = dyn_cast<PointerType>(bitcast_inst->getDestTy()))
			{
				if (struct_mapping.count(dest_type->getPointerElementType()) == 0)
				{
					AssertNonStructType(dest_type->getPointerElementType());
					return;
				}
				
				// First, if src is a pointer, look up if that src is from a malloc.
				auto *call_inst = dyn_cast<CallInst>(bitcast_inst->getOperand(0));
				if (isa<PointerType>(bitcast_inst->getSrcTy()) && call_inst && call_inst->getCalledFunction())
				{
					// Lambda as the code below is practically identical for all three cases.
					auto update_size = [&](size_t i) {
						if (auto *const_int = dyn_cast<ConstantInt>(call_inst->getArgOperand(i)))
						{
							// Take the old malloc size, divide it by the original struct size, and multiply it by the
							// inflated struct size to handle arrays of structs.
							auto struct_info = struct_mapping[dest_type->getPointerElementType()];
							auto count = const_int->getValue().udiv(struct_info->size);
							auto *new_size = ConstantInt::get(call_inst->getArgOperand(i)->getType(), 
								struct_info->inflatedSize * count);
							call_inst->setArgOperand(i, new_size);
						}
					};
					
					if (call_inst->getCalledFunction()->getName().equals("malloc"))
					{
						// If malloc, we need to update the first argument to the inflated size.
						update_size(0);
					}
					else if (call_inst->getCalledFunction()->getName().equals("calloc")
						|| call_inst->getCalledFunction()->getName().equals("realloc"))
					{
						// If calloc or realloc, we need to update the second argument to the inflated size.
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
			if (auto *load_type_ptr = dyn_cast<PointerType>(load_type))
			{
				if (struct_mapping.count(load_type_ptr->getPointerElementType()) == 0)
				{
					AssertNonStructType(load_type_ptr->getPointerElementType());
					return;
				}
				
				// Replace with pointer to inflated type.
				load_replacements[load_inst] = PointerType::getUnqual(
					struct_mapping[load_type_ptr->getPointerElementType()]->inflatedType);
			}
		}
	
		PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
			auto datalayout = M.getDataLayout();
			auto &context = M.getContext();
			
			// Iterate over all struct types. I believe this one does NOT yet deal with external struct definitions
			// (header files even, perhaps? certainly not libraries)
			for (auto st : M.getIdentifiedStructTypes()) {
				auto si = WalkStruct(st, datalayout, context);
				struct_mapping[st] = si;
			}

			for (auto &func : M) {
				// Then it is an external function, and must be linked. We can't instrument this - though it is probably
				// interesting in a later stage for inflating/deflating structs.
				if (func.isDeclaration())
				{
					continue;
				}
				// Here, we store instructions and the replacements they will get. This construction is necessary
				// because doing so invalidates existing iterators, so we cannot do it inside the loop.
				// Additionally, note that it is not really feasible to directly store the replacing instructions, as
				// they have to already be inserted to exist. Thus, we store the components we need to construct them.
				AllocaMap alloca_replacements;
				GepMap gep_replacements;
				BitcastMap bitcast_replacements;
				LoadMap load_replacements;

				for (auto &bb : func) {
					for (auto &inst : bb) {
						if (auto *gep_inst = dyn_cast<GetElementPtrInst>(&inst))
						{
							handle_gep(gep_inst, gep_replacements, context);
						}
						else if (auto *alloca_inst = dyn_cast<AllocaInst>(&inst))
						{
							handle_alloca(alloca_inst, alloca_replacements, context, datalayout);
						}
						else if (auto *bitcast_instr = dyn_cast<BitCastInst>(&inst))
						{
							handle_bitcast(bitcast_instr, bitcast_replacements, context);
						}
						else if (auto* load_instr = dyn_cast<LoadInst>(&inst))
						{
							handle_load(load_instr, load_replacements, context);
						}
						
						// TODO: store instructions?
						// not used in the toy examples we have... but perhaps if we dereference a pointer to a struct
						// to the stack it would be present.
					}
				}
				
				IRBuilder<> builder(context);
				for (const auto& [inst, tup] : alloca_replacements) {
					builder.SetInsertPoint(inst);
					// Note: the second element will be null for non-arrays.
					auto *newInst = builder.CreateAlloca(std::get<0>(tup), std::get<1>(tup));
					inst->replaceAllUsesWith(newInst);
					inst->eraseFromParent();
				}

				for (const auto& [inst, type] : bitcast_replacements) {
					builder.SetInsertPoint(inst);
					auto *newInst = builder.CreateBitCast(
						inst->getOperand(0),
						type
					);
					inst->replaceAllUsesWith(newInst);
  	  				inst->eraseFromParent();
				}

				for (const auto& [inst, type] : load_replacements) {
					builder.SetInsertPoint(inst);
					auto *newInst = builder.CreateLoad(
						type,
						inst->getPointerOperand()
					);
					inst->replaceAllUsesWith(newInst);
  	  				inst->eraseFromParent();
				}

				for (const auto& [inst, tup] : gep_replacements) {
					builder.SetInsertPoint(inst);
					auto *newInst = builder.CreateGEP(
						std::get<0>(tup),
						// NOTE: we _cannot_ move this to the other loop, because this gets altered by the alloca
						// instruction replacements!
						inst->getPointerOperand(),
						// NOTE 2: some internal llvm magic is happening with arrayref; doing it in the prior loop gives
						// strange failures.
						ArrayRef<Value*>(std::get<1>(tup))
					);
					inst->replaceAllUsesWith(newInst);
  	  				inst->eraseFromParent();
				}
			}
			// TODO:
			// 1. create runtime functions which allow the tracking of redzone regions (and crashing if accessed)
			// 2. generate functions for each struct type to properly create/remove redzones.
			// 3. directly after an alloca for a struct type, add the redzones
			// 4. directly before the ret, remove the redzones.
			// 5. verify internal overflows are now properly detected.
			// 6. adapt to external overflows.
			// 7. move on to heap structs.
			// 8. union types?
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

