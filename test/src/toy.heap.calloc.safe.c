#include <stdio.h>
#include <stdlib.h>

struct Simple {
    int zero;
    char one[2];
    char two[3];
    char three;
};

int main() {
    // Note: we very explicitly do not use struct initializer, as they get lowered to memset /
    // memcpy.
    struct Simple *example = calloc(1, sizeof(struct Simple));
    example->zero = 7;
    example->one[0] = 1;
    example->one[1] = 2;
    example->two[0] = 3;
    example->two[1] = 4;
    example->two[2] = 5;
    example->three = 6;
    // Print to see what the contents are;
    printf("zero %i\n", example->zero);
    for (int i = 0; i < 2; i++) {
        printf("one %i %i\n", i, example->one[i]);
    }
    for (int i = 0; i < 3; i++) {
        printf("two %i %i\n", i, example->two[i]);
    }
    printf("three %i\n", example->three);
    free(example);
    return 0;
}
