/* tests/m22_i386_inline_asm_labels.c - i386 inline asm and per-function labels. */

long long one(long long x) {
    __asm__ __volatile__("movl %%edx, %%eax");
    return x < 2LL;
}

long long two(long long x) {
    return x < 4LL;
}

int main(void) {
    return (one(1LL) == 1 && two(3LL) == 1) ? 42 : 1;
}
