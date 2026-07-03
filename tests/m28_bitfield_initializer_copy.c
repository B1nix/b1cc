struct S {
    volatile unsigned f0 : 15;
    signed f1 : 8;
};

static volatile struct S g = {58, 15};

int main(void) {
    volatile struct S *p = &g;

    *p = g;
    if (g.f0 != 58) return 1;
    if (g.f1 != 15) return 2;
    return 42;
}
