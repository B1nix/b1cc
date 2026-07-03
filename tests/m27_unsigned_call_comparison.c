typedef unsigned int uint32_t;

uint32_t limit = 4294956041U;

uint32_t small(void) {
    return 10U;
}

int main(void) {
    if (small() >= limit) {
        return 1;
    }
    return 42;
}
