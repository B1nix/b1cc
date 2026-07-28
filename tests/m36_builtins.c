/* M36: Comprehensive Compiler Builtins Test */

static int test_clz_ctz_popcount(void) {
    if (__builtin_clz(1) != 31) return 1;
    if (__builtin_clz(0x80000000) != 0) return 2;
    if (__builtin_clz(0x00010000) != 15) return 3;

    if (__builtin_ctz(1) != 0) return 4;
    if (__builtin_ctz(8) != 3) return 5;
    if (__builtin_ctz(0x80000000) != 31) return 6;

    if (__builtin_popcount(0) != 0) return 7;
    if (__builtin_popcount(7) != 3) return 8;
    if (__builtin_popcount(0xffffffff) != 32) return 9;
    if (__builtin_popcount(0x55555555) != 16) return 10;

    return 0;
}

static int test_add_overflow(void) {
    int res = 0;
    int ovf1 = __builtin_add_overflow(20, 22, &res);
    if (ovf1 != 0 || res != 42) return 11;

    int res2 = 0;
    int ovf2 = __builtin_add_overflow(2147483647, 1, &res2);
    if (ovf2 != 1) return 12;

    return 0;
}

static int test_expect_frame(void) {
    if (__builtin_expect(42, 1) != 42) return 13;
    if (__builtin_expect(0, 0) != 0) return 14;

    void *fa = __builtin_frame_address(0);
    if (fa == (void *)0) return 15;

    return 0;
}

int main(void) {
    int r1 = test_clz_ctz_popcount();
    if (r1 != 0) return r1;

    int r2 = test_add_overflow();
    if (r2 != 0) return r2;

    int r3 = test_expect_frame();
    if (r3 != 0) return r3;

    return 0;
}
