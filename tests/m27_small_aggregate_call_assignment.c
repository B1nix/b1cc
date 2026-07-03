struct S {
    short x;
};

static struct S g;

static struct S make_s(void) {
    struct S s;
    s.x = 0x1234;
    return s;
}

int main(void) {
    struct S *p;
    struct S l;
    p = &g;
    l = ((*p) = make_s());
    if (g.x != 0x1234) return 1;
    if (l.x != 0x1234) return 2;
    return 42;
}
