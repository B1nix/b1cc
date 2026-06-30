/* M19: long long arithmetic, casts, globals, and i386 register-pair lowering. */

long long g_ll = 5;

long long make_hi(void) {
    return (long long)1 << 33;
}

long long add_ll(long long x, long long y) {
    return x + y;
}

int main(void) {
    long long a = make_hi();
    long long b = a + 7;
    if ((int)(b >> 32) != 2) return 1;
    if ((int)b != 7) return 2;

    b = b - 3;
    if ((int)b != 4) return 3;

    long long via_arg = add_ll(a, 11);
    if ((int)(via_arg >> 32) != 2) return 9;
    if ((int)via_arg != 11) return 10;

    long long c = (long long)70000 * 70000;
    if ((int)(c / 70000) != 70000) return 4;
    if ((int)(c % 70000) != 0) return 5;

    long long d = (c ^ 3) & 7;
    if ((int)d != 3) return 6;

    if (!g_ll) return 7;
    g_ll = g_ll + 9;
    if ((int)g_ll != 14) return 8;

    return 0;
}
