#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

namespace {
	// Forward declaration to be able to use it in field info.
	class StructInfo;
	
	class FieldInfo {
	public:
		// If the field this info represents is a struct type, will contain the StructInfo for this.
		// Otherwise, will be NULL.
		std::shared_ptr<StructInfo> StructType;
		// The size (in bytes) of the field.
		// In case of a struct, this is usually slightly more than the actual struct size due to alignment.
		int size;
	};
    class StructInfo {
    public:
    	// The name of the struct.
    	std::string name;
    	// The fields present in the struct.
    	std::vector<FieldInfo> fields;
    	// The total size of the struct. Usually slightly more than the summation of the sizes of all fields due to alignment.
    	int size;
    };
    
	struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
		// Walks over a struct to provide information on its fields.
		// If this struct contains another struct, will recurse.
		std::shared_ptr<StructInfo> WalkStruct(StructType *s, DataLayout &dl)
		{
			std::vector<FieldInfo> fields;
			for (auto fieldType : s->elements())
			{
				FieldInfo field;
				field.size = dl.getTypeAllocSize(fieldType);
				if (auto* structType = dyn_cast<StructType>(fieldType))
				{
					field.StructType = WalkStruct(structType, dl);
				}
				else {
					field.StructType = NULL;
				}
				fields.push_back(field);
			}
			class StructInfo si = {
				s->getName().str(),
				fields
			};
			// To avoid a warning on implicit narrowing.
			si.size = dl.getTypeAllocSize(s);
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
				llvm::outs() << "Struct with name " << structInfo->name << " has fields:\n";
				for (auto& fieldInfo: structInfo->fields)
				{
					if (fieldInfo.StructType)
					{
						int inner_fieldCount = 0;
						llvm::outs() << "\tNested struct type " << fieldCount << ":" << fieldInfo.size << "\n";
						llvm::outs() << "\t\tStruct with name " << fieldInfo.StructType->name << " has fields:\n";
						for (auto& inner_fieldInfo: fieldInfo.StructType->fields)
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
		    // TODO: from within the pass:
			// 3. investigate what instructions are practically used to access the structs.
			// 4. add sanitation checks there, for _internal overflow_.
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

