/* tests/m28_comma_aggregate_assignment_arg.c - Comma-wrapped aggregate assignment can feed a by-value call. */

union U {
    short f0;
};

union U g = { 9 };
union U h = { 0 };

int take(union U u) {
    return u.f0;
}

int main(void) {
    union U *p;
    int side;
    int got;

    p = &h;
    side = 0;
    got = take((side = 1, ((*p) = g)));

    if (side != 1) return 1;
    if (h.f0 != 9) return 2;
    if (got != 9) return 3;
    return 42;
}
