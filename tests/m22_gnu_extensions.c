/* tests/m22_gnu_extensions.c - Tolerate common GNU C extension syntax. */
__asm__(".globl _m22_global_asm_marker\n_m22_global_asm_marker:");

__attribute__((unused)) static int m22_value = 40;

__cdecl int m22_add(__attribute__((unused)) int lhs, int rhs) {
    __asm__ __volatile__("nop");
    __asm__ __volatile__("" : : : "memory");
    return lhs + rhs;
}

int main(void) {
    return m22_add(m22_value, 2);
}
