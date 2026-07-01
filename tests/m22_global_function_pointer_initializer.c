static void *default_reallocator(void *ptr, unsigned long size) {
    if (size == 1) return ptr;
    return 0;
}

static void *(*reallocator)(void *, unsigned long) = default_reallocator;

int main(void) {
    return reallocator(0, 1) == 0 ? 42 : 1;
}
