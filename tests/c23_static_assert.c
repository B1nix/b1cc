/* tests/c23_static_assert.c — Test C23 single-argument static_assert */
static_assert(10 * 10 == 100);

int main(void) {
    static_assert(1);
    return 0;
}
