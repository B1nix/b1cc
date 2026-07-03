struct S {
    char a;
    char b;
};

static struct S g = {1, 2};

static void set(void) {
    struct S local = {4, 5};
    struct S *src = &local;
    struct S *dst = &g;
    *dst = (*src = local);
}

int main(void) {
    set();
    if (g.a != 4) return 1;
    if (g.b != 5) return 2;
    return 42;
}
