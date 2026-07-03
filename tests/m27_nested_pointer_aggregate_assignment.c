struct S {
    int x;
    signed char y;
};

static struct S src = {17, -3};
static struct S a = {0, 0};
static struct S b = {0, 0};
static struct S c = {0, 0};

int main(void) {
    struct S *pa = &a;
    struct S *pb = &b;
    c = ((*pa) = ((*pb) = src));
    if (a.x != 17 || a.y != -3) {
        return 1;
    }
    if (b.x != 17 || b.y != -3) {
        return 2;
    }
    if (c.x != 17 || c.y != -3) {
        return 3;
    }
    return 42;
}
