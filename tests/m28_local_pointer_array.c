/* tests/m28_local_pointer_array.c - Local arrays of pointers keep pointer-sized elements. */

int g;

int *retp(void) {
    return &g;
}

int main(void) {
    int *items[1];
    int *p;
    int i;

    for (i = 0; i < 1; i++) {
        items[i] = &g;
    }

    p = items[0];
    *p = 42;
    if (g != 42) return 1;

    items[0] = retp();
    p = items[0];
    *p = 42;
    return g;
}
