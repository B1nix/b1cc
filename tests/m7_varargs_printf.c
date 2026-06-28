/* tests/m7_varargs_printf.c - target ABI varargs call smoke */

int printf(char *fmt, ...);

int main(void) {
    printf("%d %d\n", 1, 2);
    return 0;
}
