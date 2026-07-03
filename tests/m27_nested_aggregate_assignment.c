struct S {
    int x;
    signed char y;
};

static struct S a = {11, 2};
static struct S b = {0, 0};

int main(void) {
    b = (a = a);
    if (a.x != 11 || a.y != 2) {
        return 1;
    }
    if (b.x != 11 || b.y != 2) {
        return 2;
    }
    return 42;
}
