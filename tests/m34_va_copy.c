/* M34: __builtin_va_copy duplicates a va_list; the copy walks the same args
 * independently of the original. */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(d, s)      __builtin_va_copy(d, s)
static int sum_twice(int n, ...) {
    va_list a; va_list b;
    va_start(a, n);
    va_copy(b, a);
    int x = va_arg(a, int);   /* first arg via a */
    int y = va_arg(b, int);   /* same first arg via the copy b */
    va_end(a); va_end(b);
    return x + y;             /* 21 + 21 = 42 */
}
int main(void) { return sum_twice(1, 21); }
