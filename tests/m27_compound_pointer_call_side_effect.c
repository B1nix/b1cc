struct S {
    char x;
};

union U {
    struct S s;
};

int g_counter = 0;
int g_value = 0x12345678;

int touch(union U a, int b, int c, union U d) {
    g_counter++;
    return b + c + a.s.x + d.s.x;
}

int main(void) {
    union U u;
    int arr[2];
    int *p;
    int i;
    char c;

    u.s.x = 1;
    arr[0] = 0x10;
    arr[1] = 0x20;
    p = &g_value;
    c = -1;

    for (i = 0; i < 2; i++) {
        (*p) ^= ((touch(u, arr[1], arr[0], u) == arr[0]) | c);
    }

    if (g_counter != 2) {
        return 1;
    }
    if (g_value != 0x12345678) {
        return 2;
    }
    return 42;
}
