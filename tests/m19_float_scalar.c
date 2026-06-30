/* tests/m19_float_scalar.c — M19 scalar float/double support.
 *
 * Exercises floating-point literals, float/double locals and globals,
 * arithmetic (+ - * /), comparisons, unary minus, int<->float conversions,
 * and float/double function arguments and return values across the call
 * boundary (System V on x86_64, AAPCS64 on arm64).
 *
 * Returns 0 on success; a non-zero code identifies the first failing check.
 */

double dsq(double x) { return x * x; }
float  fscale(float x, int n) { return x * n; }

double g_d = 1.5;
float  g_f = 0.25f;

int main(void) {
    double a = 2.0;
    double b = 3.0;

    if ((int)(a + b) != 5) return 1;
    if ((int)(a * b) != 6) return 2;
    if ((int)(b / a * 2.0) != 3) return 3;     /* 3/2*2 = 3 */
    if (!(b > a)) return 4;
    if (!(a <= b)) return 5;
    if (b == a) return 6;

    if ((int)dsq(4.0) != 16) return 7;          /* double arg + return */

    float f = 2.5f;
    if ((int)fscale(f, 4) != 10) return 8;      /* float + int args, float return */

    if ((int)(g_d * 4.0) != 6) return 9;        /* double global: 1.5*4 = 6 */
    if ((int)(g_f * 8.0) != 2) return 10;       /* float global: 0.25*8 = 2 */

    double neg = -a;
    if ((int)(neg + 5.0) != 3) return 11;       /* unary minus: -2+5 = 3 */

    int i = 7;
    double mixed = i + 0.5;                       /* int -> double */
    if ((int)mixed != 7) return 12;

    return 0;
}
