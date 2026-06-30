/* tests/c11_static_assert.c — Test C11 _Static_assert and static_assert */
_Static_assert(4 + 4 == 8, "global static assert failed");

int main(void) {
    _Static_assert(1, "local static assert failed");
    return 0;
}
