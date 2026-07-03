/* tests/m28_comma_aggregate_value.c - Aggregate-valued comma RHS copies the RHS object. */

struct S {
    unsigned int f;
};

struct S g = { 0xffffffffU };

int main(void) {
    struct S a[1];
    struct S *p;

    a[0].f = 0;
    p = &g;
    *p = (a[0], g);

    if (g.f != 0xffffffffU) return 1;
    return 42;
}
