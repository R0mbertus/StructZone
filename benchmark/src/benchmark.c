#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

enum OperandType {
    ADD,
    SUB,
    MUL,
};

struct Container {
	unsigned int val;
	enum OperandType operand;
	struct Container* next;
};

bool resolve_single(struct Container* curr) {
    if (!curr || !curr->next) {
        return false;
    }
    switch (curr->operand) {
        case ADD:
            curr->next->val = curr->next->val + curr->val;
            return true;
        case SUB:
            curr->next->val = curr->next->val + curr->val;
            return true;
        case MUL:
            curr->next->val = curr->next->val * curr->val;
            return true;
        default:
            return false;
    }
}

struct Container* resolve(struct Container* curr) {
    while (curr) {
        if (resolve_single(curr)) {
            curr = curr->next;
        } else {
            return curr;
        }
    }
    return curr;
}

struct Container* build_single(struct Container* curr) {
    struct Container *new = malloc(sizeof(struct Container));
    new->val = rand();
    switch (rand() % 3) {
        case 0:
            new->operand = ADD;
            break;
        case 1:
            new->operand = SUB;
            break;
        case 2:
            new->operand = MUL;
            break;
    }
    if (curr) {
        curr->next = new;
    }
    return new;
}

struct Container* build_ll(size_t size) {
    size_t curr_size = 1;
    struct Container *init = build_single(NULL);
    struct Container *curr = init;
    do {
        curr = build_single(curr);
        curr_size += 1;
    } while (curr_size < size);
    return init;
}

void free_ll(struct Container* curr) {
    struct Container* to_free = curr;
    while (curr) {
        curr = curr->next;
        free(to_free);
        to_free = curr;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("ERR: Expected exactly 2 CLI arguments!\n");
        return -1;
    }
    int seed = atoi(argv[1]);
    size_t ll_size = atol(argv[2]);
    //set the random seed so we get reproducible results
    srand(seed);
    // build the linked list with specified length.
    struct Container *initial = build_ll(ll_size);
    printf("Initial val: %u\n", initial->val);
    // walk through the whole linked list and compute.
    struct Container* end = resolve(initial);
    printf("End val: %u\n", end->val);
    // and finally free it.
    free_ll(initial);
    return 0;
}
