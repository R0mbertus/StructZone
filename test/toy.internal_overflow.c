#include <stdio.h>

struct TwoFields {
    int zero;
    char one[2];
    char two[3];
    char three;
};

int main() {
    struct TwoFields example = {
        7,
        {1, 2},
        {3, 4, 5},
        6
    };
    // This overflows the first field (which has a length of two), thereby also setting the 'two' array.
    // Because it starts at -4, it also overwrites the zero field, which is an int32 (thus 4 bytes long)
    // But it shouldn't touch the 'three' char.
    for (int i = -4; i < 5; i++)
    {
        example.one[i] = 0;
    }
    // Print the output to see what the contents are;
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
