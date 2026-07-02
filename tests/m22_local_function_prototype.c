/* tests/m22_local_function_prototype.c - block-scope function prototype declaration. */

int helper(void);

int main(void) {
    int helper(void);
    return helper();
}

int helper(void) {
    return 42;
}
