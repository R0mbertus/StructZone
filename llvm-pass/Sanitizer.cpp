#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

namespace {
	// Forward declaration to be able to use it in field info.
	struct StructInfo;
	
	struct FieldInfo {
		// If the field this info represents is a struct type, will contain the StructInfo for this.
		// Otherwise, will be NULL.
		std::shared_ptr<StructInfo> structInfo;
		// The size (in bytes) of the field.
		// In case of a struct, this is usually slightly more than the actual struct size due to alignment.
		long unsigned int size;
	};
	struct StructInfo {
    	// The llvm struct type.
    	StructType* type;
    	// The fields present in the struct.
    	std::vector<FieldInfo> fields;
    	// The total size of the struct. Usually slightly more than the summation of the sizes of all fields due to alignment.
    	long unsigned int size;
    };
    
	struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
		// Walks over a struct to provide information on its fields.
		// If this struct contains another struct, will recurse.
		std::shared_ptr<StructInfo> WalkStruct(StructType *s, DataLayout &dl)
		{
			std::vector<FieldInfo> fields;
			for (auto fieldType : s->elements())
			{
				FieldInfo field = {
					nullptr,
					dl.getTypeAllocSize(fieldType),
				};
				if (auto* structType = dyn_cast<StructType>(fieldType))
				{
					field.structInfo = WalkStruct(structType, dl);
				}
				fields.push_back(field);
			}
			struct StructInfo si = {
				s,
				fields,
				dl.getTypeAllocSize(s),
			};
			return std::make_shared<StructInfo>(si);
		}
	
		PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
			auto datalayout = M.getDataLayout();
			// Iterate over all struct types. I believe this one does NOT yet deal with external struct definitions (header files even, perhaps? certainly not libraries)
			std::vector<std::shared_ptr<StructInfo>> structs;
			for (auto st : M.getIdentifiedStructTypes())
			{
				structs.push_back(WalkStruct(st, datalayout));
			}
			// Printing to verify everything works as expected. just for debugging.
			for (auto &structInfo: structs)
			{
				int fieldCount = 0;
				llvm::outs() << "Struct with name " << structInfo->type->getName() << " has fields:\n";
				for (auto& fieldInfo: structInfo->fields)
				{
					if (fieldInfo.structInfo)
					{
						int inner_fieldCount = 0;
						llvm::outs() << "\tNested struct type " << fieldCount << ":" << fieldInfo.size << "\n";
						llvm::outs() << "\t\tStruct with name " << fieldInfo.structInfo->type->getName() << " has fields:\n";
						for (auto& inner_fieldInfo: fieldInfo.structInfo->fields)
						{
							llvm::outs() << "\t\t\t" << inner_fieldCount << ":" << inner_fieldInfo.size << "\n";
							inner_fieldCount += 1;
						}
					}
					else 
					{
						llvm::outs() << "\t" << fieldCount << ":" << fieldInfo.size << "\n";
					}
					fieldCount += 1;
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
				// we can then add the redzones in between each pair of fields and change the offsets.
				// NOTE: how big do the redzones need to be?
				// NOTE 2: what redzone type do we want to use? i.e., what way do we check if one is hit?
				llvm::outs() << "function with name " << func.getName() << "\n";
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

