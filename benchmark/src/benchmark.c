#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

enum OperandType {
    ADD,
    SUB,
    MUL,
    DIV
};

struct Container {
	double val;
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
        case DIV:
            curr->next->val = curr->next->val / curr->val;
            return true;
        default:
            return false;
    }
}

struct Container* resolve(struct Container* curr) {
    while (curr) {
        printf("Resolving: val is now %f and operand %i\n", curr->val, curr->operand);
        if (resolve_single(curr)) {
            curr = curr->next;
            printf("After: val is now %f and operand %i\n", curr->val, curr->operand);
        } else {
            return curr;
        }
    }
    return curr;
}

struct Container* build_single(struct Container* curr) {
    struct Container *new = malloc(sizeof(struct Container));
    double l = (double)(rand());
    double r = (double)(rand());
    double lr = l/r;
    printf("DEBUG: %f %f %f\n", l, r, lr);
    new->val = lr;
    switch (rand() % 4) {
        case 0:
            new->operand = ADD;
            break;
        case 1:
            new->operand = SUB;
            break;
        case 2:
            new->operand = MUL;
            break;
        case 3:
            new->operand = DIV;
            break;
    }
    printf("Created new container with %f, %i and %p\n", new->val, new->operand, new);
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
    printf("Initial val: %f\n", initial->val);
    // walk through the whole linked list and compute.
    struct Container* end = resolve(initial);
    printf("End val: %f\n", end->val);
    // and finally free it.
    free_ll(initial);
    return 0;
}//TODO: debug prints
