/* tests/m22_global_struct_function_pointer_field.c - function symbols in struct initializers. */

struct Ops {
    int (*call)(void);
};

static int answer(void) {
    return 42;
}

static struct Ops ops = {
    .call = answer,
};

int main(void) {
    return ops.call();
}
