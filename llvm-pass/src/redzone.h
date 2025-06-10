#include <map>
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;
#define REDZONE_SIZE 1

struct StructInfo;

struct FieldInfo
{
    // If the field this info represents is a struct type, will contain the StructInfo for this.
    // Otherwise, will be NULL.
    std::shared_ptr<StructInfo> structInfo;
    // The size (in bytes) of the field.
    // In case of a struct, this is usually slightly more than the actual struct size due to alignment.
    size_t size;
};
struct StructInfo
{
    // The llvm struct type.
    StructType *type;
    // The modified struct type that contains redzones.
    StructType *inflatedType;
    // The fields present in the struct.
    std::vector<FieldInfo> fields;
    // The total size of the struct. Usually slightly more than the summation of the sizes of all fields due to alignment.
    size_t size;
    // A mapping from offsets in the unmapped type into the mapped type.
    std::map<size_t, size_t> offsetMapping;
};

void setupRedzoneChecks(std::map<Type *, std::shared_ptr<StructInfo>>* info, Module &M);