#include <stdint.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void test_runtime_link();
void __rdzone_add(void *start, uint64_t size);
void __rdzone_check(void *probe, uint8_t op_width);
void __rdzone_rm(void *start);
void __rdzone_reset();
void __rdzone_dbg_print();
void __rdzone_heaprm(void *freed_ptr);
void __rdzone_rm_between(void *freed_ptr, size_t size);

#ifdef __cplusplus
}
#endif