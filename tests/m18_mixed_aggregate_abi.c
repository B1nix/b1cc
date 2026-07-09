/* tests/m18_mixed_aggregate_abi.c — M18 mixed integer/float aggregate ABI.
 *
 * System V x86_64 classifies each 8-byte "eightbyte" of a <=16-byte aggregate
 * independently: an eightbyte with any integer/pointer field is INTEGER (GPR),
 * an eightbyte with only float/double fields is SSE (XMM). These shapes mix the
 * two, so they must be passed/returned split across GPR and XMM registers.
 *
 * The test is self-checking (returns 0 on success) so it can be run directly and
 * also diffed against a reference compiler build. */

struct IntThenDouble { long a; double b; };   /* eb0 INTEGER, eb1 SSE  -> rdi, xmm0 */
struct DoubleThenInt { double a; long b; };    /* eb0 SSE,     eb1 INT  -> xmm0, rdi */
struct TwoIntsDouble { int a; int b; double c; }; /* eb0 INTEGER, eb1 SSE */

double sum_id(struct IntThenDouble v) { return (double)v.a + v.b; }
double sum_di(struct DoubleThenInt v) { return v.a + (double)v.b; }
double sum_iid(struct TwoIntsDouble v) { return (double)v.a + (double)v.b + v.c; }

struct IntThenDouble make_id(long a, double b) {
    struct IntThenDouble v; v.a = a; v.b = b; return v;
}
struct DoubleThenInt make_di(double a, long b) {
    struct DoubleThenInt v; v.a = a; v.b = b; return v;
}

int main(void) {
    struct IntThenDouble x = { 7, 2.5 };
    if ((int)sum_id(x) != 9) return 1;

    struct DoubleThenInt y = { 3.5, 4 };
    if ((int)sum_di(y) != 7) return 2;

    struct TwoIntsDouble z = { 10, 20, 12.0 };
    if ((int)sum_iid(z) != 42) return 3;

    /* by-value round trip through a returning function */
    struct IntThenDouble r = make_id(100, 0.25);
    if (r.a != 100) return 4;
    if ((int)(r.b * 100.0) != 25) return 5;

    struct DoubleThenInt r2 = make_di(6.75, 9);
    if ((int)(r2.a * 100.0) != 675) return 6;
    if (r2.b != 9) return 7;

    return 0;
}
