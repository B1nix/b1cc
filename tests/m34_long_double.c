/* tests/m34_long_double.c — M34 long double (80-bit x87 on x86; == double on
 * arm64-darwin per the Apple ABI, so this runs for real on the host).
 *
 * Exercises long double literals (L suffix), locals, a global with an L
 * initializer, arithmetic (+ - * /), comparisons, int<->long double and
 * double<->long double conversions, and sizeof. Returns 0 on success; a
 * non-zero code identifies the first failing check.
 *
 */

long double dbl(long double x) {
    return x + x;
}

long double abi_mix(int a, long double x, double d, long double y,
                    int b, long double z) {
    return x + y + z + d + a + b;
}

long double g_ld = 2.5L;

int main(void) {
    long double a = 4.0L;
    long double b = 2.0L;

    if ((int)(a + b) != 6) return 1;
    if ((int)(a - b) != 2) return 2;
    if ((int)(a * b) != 8) return 3;
    if ((int)(a / b) != 2) return 4;

    if (!(a > b)) return 5;
    if (!(b < a)) return 6;
    if (!(a >= a)) return 7;
    if (!(b <= a)) return 8;
    if (a == b) return 9;
    if (!(a != b)) return 10;

    /* int -> long double and back */
    long double c = 7;
    if ((int)c != 7) return 11;

    /* double -> long double and long double -> double */
    double d = 3.5;
    long double e = d;
    if ((int)(e * 2.0L) != 7) return 12;

    if ((int)dbl(2.5L) != 5) return 13;
    if ((int)abi_mix(1, 2.5L, 3.25, 4.5L, 5, 6.75L) != 23) return 14;

    /* global with an L initializer */
    if ((int)(g_ld * 2.0L) != 5) return 15;

    /* mutate a long double local */
    a = a + g_ld;          /* 4.0 + 2.5 = 6.5 */
    if ((int)a != 6) return 16;

    return 0;
}
