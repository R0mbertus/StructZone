#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

namespace {
	struct StructZoneSanitizer : PassInfoMixin<StructZoneSanitizer> {
		PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
			auto datalayout = M.getDataLayout();
			// Iterate over all struct types. I believe this one does NOT yet deal with external struct definitions (header files even, perhaps? certainly not libraries)
			for (auto st : M.getIdentifiedStructTypes())
			{
				llvm::outs() << st->getName() << "\n";
				for (auto subType : st->elements())
				{
					// for each field, this prints the type id (not ideal, but things don't always have names) and the size in bytes.
					llvm::outs() << "\t" << subType->getTypeID();
					llvm::outs() << " - " << datalayout.getTypeAllocSize(subType) << "\n";
				}
				// TODO: nested structs. move logic out to helper function so we can recurse :thumbs up emoji:
			}
		  // TODO: from within the pass:
			// 2. gather data on structs / their sizes and so on.
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

