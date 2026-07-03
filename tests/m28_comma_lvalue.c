/* tests/m28_comma_lvalue.c - Comma expressions can yield assignable lvalues. */

struct Pair {
    int a;
};

int bump(int *p) {
    *p = *p + 1;
    return *p;
}

int main(void) {
    int side = 0;
    int value = 3;
    int arr[2];
    struct Pair p;
    int *ptr;

    arr[0] = 10;
    arr[1] = 20;
    p.a = 1;
    ptr = arr;

    (bump(&side), value) = 41;
    if (side != 1) return 1;
    if (value != 41) return 2;

    (bump(&side), ptr)[1] = 32;
    if (side != 2) return 3;
    if (arr[1] != 32) return 4;

    (bump(&side), p).a = 9;
    if (side != 3) return 5;
    if (p.a != 9) return 6;

    return 42;
}
