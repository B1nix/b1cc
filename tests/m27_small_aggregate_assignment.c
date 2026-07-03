struct S {
    signed char x;
};

static struct S g = {99};

int main(void) {
    struct S local = {7};
    g = g;
    local = local;
    if (g.x != 99) {
        return 1;
    }
    if (local.x != 7) {
        return 2;
    }
    return 42;
}
