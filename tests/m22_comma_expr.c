/* tests/m22_comma_expr.c - Covered comma operator positions. */

int add_one(int x) {
    return x + 1;
}

int main(void) {
    int a = 0;
    int b = 0;
    int (*fp)(int);
    a = 5, b = a + 37;
    if (a != 5) return 1;
    if (b != 42) return 2;
    while (a = a - 1, a > 2) {
        b = b + 1;
    }
    if (b != 44) return 3;
    for (a = 0; a < 3, a < 2; a++, b++) {
    }
    if (b != 46) return 4;
    fp = add_one;
    b = (a++, fp)(41);
    if (b != 42) return 5;
    for (a = 0, b = 0; a < 2; a = a + 1, b = b + 2) {
    }
    if (b != 4) return 6;
    return 42;
}
