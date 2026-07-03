/* tests/m28_global_pointer_array.c - Global arrays of pointers keep pointer-sized elements. */

int g;
int *items[1] = { &g };

int main(void) {
    int *p;

    p = items[0];
    *p = 42;
    return g;
}
