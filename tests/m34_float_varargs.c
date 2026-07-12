typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

double sum_double(int count, ...) {
    va_list ap;
    va_start(ap, count);
    double total = 0.0;
    for (int i = 0; i < count; ++i) total = total + va_arg(ap, double);
    va_end(ap);
    return total;
}

int main(void) {
    if (sum_double(3, 1.5, 2.25, 3.25) != 7.0) return 1;
    return 42;
}
