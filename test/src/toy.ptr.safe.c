#include <stdio.h>
#include <stdlib.h>

struct Simple {
    int zero;
    char one[2];
    char two[3];
    char three;
};

struct Nested {
    char zero[4];
    struct Simple *one;
};

int main() {
    // Note: we very explicitly do not use struct initializer, as they get lowered to memset /
    // memcpy.
    struct Nested example;
    example.zero[0] = 8;
    example.zero[1] = 9;
    example.zero[2] = 10;
    example.zero[3] = 11;
    struct Simple *inner = malloc(sizeof(struct Simple));
    inner->zero = 7;
    inner->one[0] = 1;
    inner->one[1] = 2;
    inner->two[0] = 3;
    inner->two[1] = 4;
    inner->two[2] = 5;
    inner->three = 6;
    example.one = inner;
    // Print to see what the contents are;
    for (int i = 0; i < 4; i++) {
        printf("outer zero %i %i\n", i, example.zero[i]);
    }
    printf("inner zero %i\n", example.one->zero);
    for (int i = 0; i < 2; i++) {
        printf("one %i %i\n", i, example.one->one[i]);
    }
    for (int i = 0; i < 3; i++) {
        printf("two %i %i\n", i, example.one->two[i]);
    }
    printf("three %i\n", example.one->three);
    return 0;
}
