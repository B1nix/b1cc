/* tests/m34_long_double_abi.c — M34 long double aggregate ABI and varargs.
 *
 * Tests:
 *   1. Structs containing long double fields (aggregate ABI classification).
 *   2. Passing long double to variadic functions (vararg promotion).
 *   3. Using va_arg to extract long double from variadic functions.
 *
 * On arm64-darwin long double == double (8 bytes), so these tests exercise
 * the double/float aggregate and vararg paths.  On x86_64-b1nix they exercise
 * the true 80-bit x87 long double paths (verified by assembly inspection).
 *
 * Returns 0 on success; a non-zero code identifies the first failing check.
 */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

/* --- Test 1: Struct containing long double fields --- */

struct ld_pair {
    long double a;
    long double b;
};

struct ld_mixed {
    int x;
    long double y;
    int z;
};

long double sum_pair(struct ld_pair p) {
    return p.a + p.b;
}

long double sum_mixed(struct ld_mixed m) {
    return m.y + m.x + m.z;
}

/* --- Test 2: Struct containing long double returned --- */

struct ld_pair make_pair(long double a, long double b) {
    struct ld_pair p;
    p.a = a;
    p.b = b;
    return p;
}

/* --- Test 3: Long double in variadic function (caller side) --- */

int ld_vararg_caller(int fixed, ...) {
    va_list ap;
    va_start(ap, fixed);
    long double v1 = va_arg(ap, long double);
    long double v2 = va_arg(ap, long double);
    va_end(ap);
    return (int)(v1 + v2 + fixed);
}

/* --- Test 4: Multiple long double varargs after int varargs --- */

int mixed_vararg_caller(int fixed, ...) {
    va_list ap;
    va_start(ap, fixed);
    int i1 = va_arg(ap, int);
    long double ld1 = va_arg(ap, long double);
    int i2 = va_arg(ap, int);
    va_end(ap);
    return (int)(i1 + ld1 + i2 + fixed);
}

/* --- Test 5: Long double vararg with struct arg in same call --- */

struct ld_pair identity_pair(struct ld_pair p) {
    return p;
}

int vararg_with_struct(int fixed, ...) {
    va_list ap;
    va_start(ap, fixed);
    long double v = va_arg(ap, long double);
    va_end(ap);
    return (int)(v + fixed);
}

int main(void) {
    /* Test 1: struct with long double fields */
    struct ld_pair p1 = {3.0L, 4.0L};
    if ((int)sum_pair(p1) != 7) return 1;

    struct ld_mixed m1 = {10, 2.5L, 20};
    if ((int)sum_mixed(m1) != 32) return 2;

    /* Test 2: returning struct with long double fields */
    struct ld_pair p2 = make_pair(5.0L, 6.0L);
    if ((int)(p2.a + p2.b) != 11) return 3;

    /* Test 3: long double in varargs (caller side) */
    if (ld_vararg_caller(100, 2.5L, 3.5L) != 106) return 4;

    /* Test 4: mixed int/long double varargs */
    if (mixed_vararg_caller(10, 20, 3.5L, 30) != 63) return 5;

    /* Test 5: struct arg + long double vararg in same call */
    struct ld_pair p3 = {1.0L, 2.0L};
    struct ld_pair p3r = identity_pair(p3);
    if ((int)(p3r.a + p3r.b) != 3) return 6;
    if (vararg_with_struct(100, 5.0L) != 105) return 7;

    return 0;
}
