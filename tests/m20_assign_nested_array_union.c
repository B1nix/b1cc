/* tests/m20_assign_nested_array_union.c - Nested array/union field access inside aggregate-copy regression shapes. */

struct Nested {
    int arr[3];
    union {
        int x;
        char y;
    } u;
};

struct Outer {
    struct Nested nested;
    int tail;
};

struct Outer global_src = { { { 10, 20, 30 }, { 40 } }, 50 };
struct Outer global_dst;

int main(void) {
    struct Outer local_src = { { { 50, 60, 70 }, { 80 } }, 90 };
    struct Outer local_dst;
    local_dst = local_src;

    if (local_dst.nested.arr[0] != 50) return 1;
    if (local_dst.nested.arr[1] != 60) return 2;
    if (local_dst.nested.arr[2] != 70) return 3;
    if (local_dst.nested.u.x != 80) return 4;
    if (local_dst.tail != 90) return 5;

    global_dst = global_src;
    if (global_dst.nested.arr[0] != 10) return 6;
    if (global_dst.nested.arr[1] != 20) return 7;
    if (global_dst.nested.arr[2] != 30) return 8;
    if (global_dst.nested.u.x != 40) return 9;
    if (global_dst.tail != 50) return 10;

    return 42;
}
