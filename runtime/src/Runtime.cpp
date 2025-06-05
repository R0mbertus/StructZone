#include <stdio.h>

namespace {
    // You can write anything here and it will be invisible to the outside as it
    // has internal linkage.
void test_runtime_link(){
    printf("runtime initalised!\n");
}
}

extern "C" {
    void test_runtime_link();
}