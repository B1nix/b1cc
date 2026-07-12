/* M34: old-style (K&R) function definitions are supported. Parameter names are
 * declared in a list between `)` and the body; undeclared names default to int. */
int add(a, b) int a; int b; { return a + b; }
long with_ptr(n, p) long n; int *p; { return n + *p; }
int three(a, b, c) int a, b, c; { return a + b + c; }

int main(void) {
    if (add(40, 2) != 42) return 1;
    int v = 2;
    if (with_ptr(40, &v) != 42) return 2;
    if (three(20, 15, 7) != 42) return 3;
    return 42;
}
