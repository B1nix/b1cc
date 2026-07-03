struct S {
    char x;
};

static struct S g;
static struct S discarded;
static int side;

struct S make_s(void) {
    struct S s;
    side = side + 1;
    s.x = 40;
    return s;
}

int take_s(struct S s) {
    return s.x;
}

int main(void) {
    int got;

    discarded.x = 1;
    g = (discarded, make_s());
    got = take_s((discarded, make_s()));

    if (g.x != 40) return 1;
    if (got != 40) return 2;
    if (side != 2) return 3;
    return 42;
}
