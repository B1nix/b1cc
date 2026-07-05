#include <stdio.h>
#include <stdlib.h>

struct flex {
    int n;
    int data[];
};

struct flex2 {
    char a;
    int data[];
};

int main() {
    if (sizeof(struct flex) != sizeof(int)) return 1;
    if (sizeof(struct flex2) != sizeof(int)) return 2;

    struct flex *p = malloc(sizeof(struct flex) + 5 * sizeof(int));
    p->n = 5;
    for (int i = 0; i < 5; i++) {
        p->data[i] = i * 10;
    }
    if (p->data[0] != 0) return 3;
    if (p->data[4] != 40) return 4;

    free(p);
    printf("ok flexible_array: sizeof(struct flex) = %zu\n", sizeof(struct flex));
    return 0;
}
