#include <stdint.h>
#include <stdlib.h>
extern "C" void test_runtime_link();
extern "C" void __rdzone_add(void *start, uint64_t size);
extern "C" void __rdzone_check(void *probe, uint8_t op_width);
extern "C" void __rdzone_rm(void *start);
extern "C" void __rdzone_reset();
extern "C" void __rdzone_dbg_print();
extern "C" void __rdzone_heaprm(void *freed_ptr);
extern "C" void __rdzone_rm_between(void *freed_ptr, size_t size);