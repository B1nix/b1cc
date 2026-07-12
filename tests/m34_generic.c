/* tests/m34_generic.c — Test M34 _Generic selections */
struct A { int x; };
struct B { int y; };

int main(void) {
    char c = 'a';
    int i = 42;
    long l = 100;
    char *ptr = 0;
    struct A a;

    int res1 = _Generic(c, char: 1, int: 2, long: 3, default: 4);
    int res2 = _Generic(i, char: 1, int: 2, long: 3, default: 4);
    int res3 = _Generic(l, char: 1, int: 2, long: 3, default: 4);
    int res4 = _Generic(ptr, char *: 10, int *: 20, default: 30);
    int res5 = _Generic(a, struct A: 50, struct B: 60, default: 70);

    if (res1 != 1) return 1;
    if (res2 != 2) return 2;
    if (res3 != 3) return 3;
    if (res4 != 10) return 4;
    if (res5 != 50) return 5;

    return 0;
}
