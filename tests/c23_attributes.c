/* tests/c23_attributes.c — Test C23 standard attribute syntax skipping */
[[deprecated]] void old_func(void) {}

int main(void) {
    [[maybe_unused]] int x = 42;
    return 0;
}
