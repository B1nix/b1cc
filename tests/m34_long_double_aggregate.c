/* M34/M34: active-target long double aggregate ABI smoke. */
struct Pair {
    long double a;
    long double b;
};

static long double add_pair(struct Pair p) {
    return p.a + p.b;
}

int main(void) {
    struct Pair p = {1.25L, 2.75L};
    long double v = add_pair(p);
    if (v < 3.999L || v > 4.001L) return 1;
    return 42;
}
