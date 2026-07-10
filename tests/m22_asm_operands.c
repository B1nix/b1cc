/* tests/m22_asm_operands.c - Test GNU asm operand and register constraint mapping. */

int test_asm_add(int a, int b) {
    int res;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("mov %2, %0\n    add %1, %0" : "=r"(res) : "r"(a), "r"(b));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, %2\n    add %0, %0, %1" : "=r"(res) : "r"(a), "r"(b));
#else
    res = a + b;
#endif
    return res;
}

int main(void) {
    int x = 40;
    int y = 2;
    int r = test_asm_add(x, y);
    if (r != 42) {
        return 1;
    }
    return 0;
}
