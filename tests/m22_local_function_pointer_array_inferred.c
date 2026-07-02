/* tests/m22_local_function_pointer_array_inferred.c - local inferred function pointer arrays. */

static int forty(void) {
    return 40;
}

static int two(void) {
    return 2;
}

int main(void) {
    int (*handlers[])(void) = {forty, two};
    return handlers[0]() + handlers[1]();
}
