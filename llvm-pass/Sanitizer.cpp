#include <map>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

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
			size_t mapped_offset = 0;
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
				offset_mapping[base_offset] = mapped_offset;
				base_offset += field.size;
				mapped_offset += (field.size + REDZONE_SIZE);
			}
			// The last redzone (at least for now) is superfluous since it does not protect against internal overflows.
			mappedFields.pop_back();
			
			// If the inflated type doesn't exist yet, create it. For recursion, it will already exist, so lets not create duplicates.
			auto inflated_type = StructType::getTypeByName(ctx, s->getName().str() + ".inflated");
			if (inflated_type == NULL)
			{
				inflated_type = StructType::create(ctx, s->getName().str()+".inflated");
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
			auto datalayout = M.getDataLayout();
			auto &context = M.getContext();
			// Iterate over all struct types. I believe this one does NOT yet deal with external struct definitions (header files even, perhaps? certainly not libraries)
			std::vector<std::shared_ptr<StructInfo>> structs;
			for (auto st : M.getIdentifiedStructTypes())
			{
				structs.push_back(WalkStruct(st, datalayout, context));
			}
			// Printing to verify everything works as expected. just for debugging.
			for (auto &structInfo: structs)
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
				// for now, track:
				// - alloca's with a known struct type.
				// - getelementptr whose source is a known struct instance (i.e. an alloca)
				// that gives all accesses to the struct.
				// we can change the struct accesses through the offset mapping, and the redzone size through the appropriately named constant.
				// NOTE: what redzone type do we want to use? i.e., what way do we check if one is hit?
				outs() << "function with name " << func.getName() << "\n";
			}

		  // TODO: from within the pass:
			// 3. investigate what instructions are practically used to access the structs.
			// 4. add sanitation checks there, for _internal overflow_.
			// (i.e., whenever a load happens whose source is a getelementptr associated to a struct instance, first insert a call to a function which crashes if the memory is a redzone)
			// 5. verify the program now crashes on internal overflow, but not on the safe variant.
			// 6. move on to external overflows.
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

