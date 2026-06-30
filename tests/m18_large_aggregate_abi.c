/* tests/m18_large_aggregate_abi.c — Milestone M18 large aggregate ABI passing/returns */

struct Large {
    long a;
    long b;
    long c;
    long d;
};

struct Large make_large(long a, long b, long c, long d) {
    struct Large l = { .a = a, .b = b, .c = c, .d = d };
    return l;
}

long sum_large(struct Large l) {
    return l.a + l.b + l.c + l.d;
}

long stack_large(int a, int b, int c, int d, int e, int f, int g, int h, struct Large l) {
    return a + b + c + d + e + f + g + h + l.a + l.b + l.c + l.d;
}

int main(void) {
    struct Large l = make_large(100, 200, 300, 400);
    if (l.a != 100) return 1;
    if (l.b != 200) return 2;
    if (l.c != 300) return 3;
    if (l.d != 400) return 4;

    if (sum_large(l) != 1000) return 5;
    if (stack_large(1, 2, 3, 4, 5, 6, 7, 8, l) != 1036) return 6;

    return 0;
}
