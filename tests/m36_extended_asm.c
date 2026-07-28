/* M36: Comprehensive Extended Inline Assembly Test */

static int test_basic_asm(int a, int b) {
    int out = 0;
#if defined(__x86_64__)
    __asm__ volatile (
        "movl %1, %0\n\t"
        "addl %2, %0"
        : "=r" (out)
        : "r" (a), "r" (b)
        : "cc"
    );
#elif defined(__aarch64__)
    __asm__ volatile (
        "add %0, %1, %2"
        : "=r" (out)
        : "r" (a), "r" (b)
        : "cc"
    );
#else
    out = a + b;
#endif
    return out;
}

static int test_multi_operand(int x, int y, int z) {
    int res1 = 0, res2 = 0;
#if defined(__x86_64__)
    __asm__ volatile (
        "movl %2, %0\n\t"
        "addl %3, %0\n\t"
        "movl %4, %1"
        : "=r" (res1), "=r" (res2)
        : "r" (x), "r" (y), "r" (z)
        : "cc"
    );
#elif defined(__aarch64__)
    __asm__ volatile (
        "add %0, %2, %3\n\t"
        "mov %1, %4"
        : "=r" (res1), "=r" (res2)
        : "r" (x), "r" (y), "r" (z)
        : "cc"
    );
#else
    res1 = x + y;
    res2 = z;
#endif
    return res1 + res2;
}

int main(void) {
    if (test_basic_asm(10, 32) != 42) return 1;
    if (test_basic_asm(100, -58) != 42) return 2;
    if (test_multi_operand(10, 20, 12) != 42) return 3;
    return 0;
}
