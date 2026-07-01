static int one(void) {
    return 42;
}

static int (*funcs[2])(void);

int main(void) {
    funcs[1] = one;
    return funcs[1]();
}
