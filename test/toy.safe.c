#include <stdio.h>

struct TwoFields {
    int zero;
    char one[2];
    char two[3];
    char three;
};

int main() {
    struct TwoFields example = {
        -1,
        {0, 1},
        {2, 3, 4},
        5
    };
    printf("zero %i\n", example.zero);
    for (int i = 0; i < 2; i++)
    {
        printf("one %i %i\n", i, example.one[i]);
    }
    for (int i = 0; i < 3; i++)
    {
        printf("two %i %i\n", i, example.two[i]);
    }
    printf("three %i\n", example.three);
    return 0;
}
