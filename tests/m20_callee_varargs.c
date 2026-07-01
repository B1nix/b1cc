/* tests/m20_callee_varargs.c - Callee-side varargs test */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

int printf(const char *fmt, ...);

int sum(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += va_arg(ap, int);
    }
    va_end(ap);
    return total;
}

int main(void) {
    int s = sum(3, 10, 20, 30);
    if (s != 60) {
        return 1;
    }
    return 42;
}
