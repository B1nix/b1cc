/* tests/m20_aggregate_copy_nested.c - Struct assignment copies nested aggregates by value. */

struct Inner {
    int a;
    int b;
};

struct Outer {
    struct Inner inner;
    int tail;
};

struct Outer global_src = { { 1, 2 }, 4 };
struct Outer global_dst;

int main(void) {
    struct Outer local_src = { { 5, 6 }, 8 };
    struct Outer local_dst = local_src;

    local_src.tail = 80;
    if (local_dst.inner.a != 5) return 1;
    if (local_dst.inner.b != 6) return 2;
    if (local_dst.tail != 8) return 3;
    if (local_src.tail != 80) return 4;

    global_dst = global_src;
    global_src.tail = 40;
    if (global_dst.inner.a != 1) return 5;
    if (global_dst.inner.b != 2) return 6;
    if (global_dst.tail != 4) return 7;
    if (global_src.tail != 40) return 8;

    return 42;
}
