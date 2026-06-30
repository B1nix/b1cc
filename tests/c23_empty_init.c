/* tests/c23_empty_init.c — Test C23 empty initializer list {} */
struct S {
    int a;
    int b;
};

int main(void) {
    int arr[3] = {};
    struct S s = {};

    if (arr[0] != 0 || arr[1] != 0 || arr[2] != 0) return 1;
    if (s.a != 0 || s.b != 0) return 2;

    return 0;
}
