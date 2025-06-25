#include <stdio.h>
#include <stdlib.h>

struct Simple {
    int zero;
    char one[2];
    char two[3];
    char three;
};

void print(struct Simple *example) {
    printf("zero %i\n", example->zero);
    for (int i = 0; i < 2; i++) {
        printf("one %i %i\n", i, example->one[i]);
    }
    for (int i = 0; i < 3; i++) {
        printf("two %i %i\n", i, example->two[i]);
    }
    printf("three %i\n", example->three);
}

int main() {
    // Note: we very explicitly do not use struct initializer, as they get lowered to memset /
    // memcpy.
    struct Simple *example;

    struct Simple if_example = {.zero = 0, .one = {1, 2}, .two = {3, 4, 5}, .three = 6};
    struct Simple else_example = {.zero = 1, .one = {2, 3}, .two = {4, 5, 6}, .three = 7};

    if (rand() % 2) {
        example = &if_example;
    } else {
        example = &else_example;
    }

    // Print to see what the contents are;
    print(example);
    return 0;
}
