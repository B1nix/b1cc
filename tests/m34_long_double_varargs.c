/* M34: long double varargs follows the active target promotion/ABI rules. */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

static long double sum_long_double(int count, ...) {
    va_list ap;
    va_start(ap, count);
    long double total = 0.0L;
    for (int i = 0; i < count; ++i) total = total + va_arg(ap, long double);
    va_end(ap);
    return total;
}

int main(void) {
    long double v = sum_long_double(2, 1.25L, 2.75L);
    if (v < 3.999L || v > 4.001L) return 1;
    return 42;
}
