#ifndef FUNCTION_TRANSFORMER_H
#define FUNCTION_TRANSFORMER_H
#include "redzone.h"
using namespace llvm;

void transformFuncSig(Function *func, 
std::map<Type *,std::shared_ptr<StructInfo>> *struct_mapping);

//Some functions I need to build need to not be transformed by the gep transformation
//This function triggers their delayed population.

void populate_delicate_functions();
#endif
