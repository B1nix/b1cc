int atomic_load_alias(int *ptr) __asm__("__atomic_load");

int atomic_load_alias(int *ptr) {
    return *ptr;
}

int main(void) {
    int value = 42;
    return atomic_load_alias(&value);
}
