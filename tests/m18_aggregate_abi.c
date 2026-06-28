/* tests/m18_aggregate_abi.c — Milestone M18 aggregate ABI passing/returns */

struct Pair {
    int x;
    int y;
};

struct Wide {
    long a;
    long b;
};

struct Pair make_pair(int x, int y) {
    struct Pair p = { .x = x, .y = y };
    return p;
}

int sum_pair(struct Pair p) {
    return p.x + p.y;
}

struct Wide make_wide(long a, long b) {
    struct Wide w = { .a = a, .b = b };
    return w;
}

long sum_wide(struct Wide w) {
    return w.a + w.b;
}

int stack_pair(int a, int b, int c, int d, int e, int f, int g, struct Pair p) {
    return a + b + c + d + e + f + g + p.x + p.y;
}

int main(void) {
    struct Pair p = make_pair(11, 31);
    if (p.x != 11) return 1;
    if (p.y != 31) return 2;
    if (sum_pair(p) != 42) return 3;
    if (stack_pair(1, 2, 3, 4, 5, 6, 7, p) != 70) return 7;

    struct Wide w = make_wide(10000000000, 23);
    if (w.a != 10000000000) return 4;
    if (w.b != 23) return 5;
    if (sum_wide(w) != 10000000023) return 6;

    return 0;
}
