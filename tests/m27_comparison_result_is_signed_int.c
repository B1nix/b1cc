typedef signed char int8_t;

int main(void) {
    int8_t n = -1;
    if (((1U >= 0U) < n)) return 1;
    if (((0U > 1U) < n)) return 2;
    return 42;
}
