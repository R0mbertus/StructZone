#include <stdio.h>

struct Simple {
    int zero;
    char one[2];
    char two[3];
    char three;
};

struct Nested {
    int zero;
    struct Simple one;
};

int main() {
    // Note: we very explicitly do not use struct initializer, as they get lowered to memset /
    // memcpy.
    struct Nested example;
    example.zero = 8;
    example.one.zero = 7;
    example.one.one[0] = 1;
    example.one.one[1] = 2;
    example.one.two[0] = 3;
    example.one.two[1] = 4;
    example.one.two[2] = 5;
    example.one.three = 6;
    // We now overflow from one into two, but we _also_ underflow into one and the outer zero field.
    for (int i = -8; i < 5; i++) {
        example.one.one[i] = 0;
    }
    // Print to see what the contents are;
    printf("outer zero %i\n", example.zero);
    printf("inner zero %i\n", example.one.zero);
    for (int i = 0; i < 2; i++) {
        printf("one %i %i\n", i, example.one.one[i]);
    }
    for (int i = 0; i < 3; i++) {
        printf("two %i %i\n", i, example.one.two[i]);
    }
    printf("three %i\n", example.one.three);
    return 0;
}
