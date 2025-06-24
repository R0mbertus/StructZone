#include <stdio.h>

struct Simple {
    int zero;
    char one[2];
    char two[3];
    char three;
};

struct Nested {
    int zero;
    struct Simple one[1];
};

int main() {
    // Note: we very explicitly do not use struct initializer, as they get lowered to memset /
    // memcpy.
    struct Nested example;
    example.zero = 8;
    example.one[0].zero = 7;
    example.one[0].one[0] = 1;
    example.one[0].one[1] = 2;
    example.one[0].two[0] = 3;
    example.one[0].two[1] = 4;
    example.one[0].two[2] = 5;
    example.one[0].three = 6;
    // Print to see what the contents are;
    printf("outer zero %i\n", example.zero);
    printf("inner zero %i\n", example.one[0].zero);
    for (int i = 0; i < 2; i++) {
        printf("one %i %i\n", i, example.one[0].one[i]);
    }
    for (int i = 0; i < 3; i++) {
        printf("two %i %i\n", i, example.one[0].two[i]);
    }
    printf("three %i\n", example.one[0].three);
    return 0;
}
