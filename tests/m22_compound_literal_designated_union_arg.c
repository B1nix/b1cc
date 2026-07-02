/* tests/m22_compound_literal_designated_union_arg.c - designated union compound literal argument. */

union Value {
    void *ptr;
    long word;
};

static int check(union Value value) {
    return value.ptr == 0 ? 42 : 1;
}

int main(void) {
    return check((union Value){.ptr = 0});
}
