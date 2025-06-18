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
    struct Simple *examples = malloc(2 * sizeof(struct Simple));
    examples[0].zero = 7;
    examples[0].one[0] = 1;
    examples[0].one[1] = 2;
    examples[0].two[0] = 3;
    examples[0].two[1] = 4;
    examples[0].two[2] = 5;
    examples[0].three = 6;
    // explicitly zero-ing out everything to avoid uninitialized reads
    examples[1].zero = 0;
    examples[1].one[0] = 0;
    examples[1].one[1] = 0;
    examples[1].two[0] = 0;
    examples[1].two[1] = 0;
    examples[1].two[2] = 0;
    // overflow from the second struct back into the first.
    // we now expect everything but the first field of the first struct to be overwritten.
    for (int i = -12; i < 5; i++) {
        examples[1].one[i] = 0;
    }
    // Print to see what the contents are;
    for (int x = 0; x < 2; x++) {
        printf("zero %i\n", examples[x].zero);
        for (int i = 0; i < 2; i++) {
            printf("one %i %i\n", i, examples[x].one[i]);
        }
        for (int i = 0; i < 3; i++) {
            printf("two %i %i\n", i, examples[x].two[i]);
        }
        printf("three %i\n", examples[x].three);
    }
    free(examples);
    return 0;
}
