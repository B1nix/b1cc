struct S {
    long long s;
    unsigned long long u;
};

static struct S g = {9LL, 0x831eb2391cb4e76aULL};

int main(void) {
    if (g.s != 9LL) return 1;
    if (g.u != 0x831eb2391cb4e76aULL) return 2;
    return 42;
}
