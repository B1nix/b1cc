struct S {
    int x;
};

static struct S g = {7};
static struct S *a[2] = {&g, &g};
static struct S *b[3] = {&g, &g, &g};

int main(void) {
    struct S local = {9};
    struct S *tmp[3];
    int i;
    for (i = 0; i < 3; i = i + 1) {
        tmp[i] = (void *)0;
    }

    b[1] = (a[1] = (tmp[2] = &local));
    if (b[1] != &local) return 1;
    if (a[1] != &local) return 2;
    if (tmp[2] != &local) return 3;
    return 42;
}
