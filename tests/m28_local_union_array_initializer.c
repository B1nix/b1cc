union U {
    const int f0;
};

int main(void) {
    union U a[3] = {{-9}, {-9}, {-9}};
    if (a[0].f0 != -9) return 1;
    if (a[1].f0 != -9) return 2;
    if (a[2].f0 != -9) return 3;
    return 42;
}
