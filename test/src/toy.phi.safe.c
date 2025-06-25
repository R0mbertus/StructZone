#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

    struct Simple if_example;
    struct Simple else_example;

    if (rand() % 2) {
        if_example.zero = 0;
        if_example.one[0] = 1;
        if_example.one[1] = 2;
        if_example.two[0] = 3;
        if_example.two[1] = 4;
        if_example.two[2] = 5;
        if_example.three = 6;
        example = &if_example;
    } else {
        else_example.zero = 7;
        else_example.one[0] = 8;
        else_example.one[1] = 9;
        else_example.two[0] = 10;
        else_example.two[1] = 11;
        else_example.two[2] = 12;
        else_example.three = 13;
        example = &else_example;
    }

    // Print to see what the contents are;
    print(example);
    return 0;
}
