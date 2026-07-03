struct S {
    int a;
    int b;
};

union U {
    int x;
};

static struct S g = {19, 23};
static int calls;

union U bump(void) {
    union U u;
    calls = calls + 1;
    u.x = 7;
    return u;
}

int main(void) {
    struct S local;

    local.a = 0;
    local.b = 0;
    local = (bump(), g);

    if (calls != 1) return 1;
    if (local.a != 19) return 2;
    if (local.b != 23) return 3;
    return 42;
}
