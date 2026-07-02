/* tests/m22_aggregate_return_call.c - return aggregate result of another call. */

struct Pair {
    long a;
    long b;
};

static struct Pair make_pair(void) {
    struct Pair p = {20, 22};
    return p;
}

static struct Pair wrap_pair(void) {
    return make_pair();
}

int main(void) {
    struct Pair p = wrap_pair();
    return (int)(p.a + p.b);
}
