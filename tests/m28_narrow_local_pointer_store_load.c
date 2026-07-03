/* tests/m28_narrow_local_pointer_store_load.c - Narrow locals reload with declared signedness after pointer stores. */

int main(void) {
    signed char s;
    unsigned char u;
    signed char *sp;
    unsigned char *up;
    int a;
    int b;

    s = -5;
    u = 250;
    sp = &s;
    up = &u;

    *sp = 6;
    *up = 6;
    a = s;
    b = u;

    if (a != 6) return 1;
    if (b != 6) return 2;
    return 42;
}
