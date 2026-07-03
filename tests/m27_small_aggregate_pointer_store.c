struct S {
    int x;
    signed char y;
};

static struct S g = {1234567, -5};

int main(void) {
    struct S *p;
    p = &g;
    (*p) = g;
    if (g.x != 1234567) return 1;
    if (g.y != -5) return 2;
    return 42;
}
