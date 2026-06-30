/* tests/c23_nullptr.c — Test C23 nullptr literal */
int main(void) {
    char *ptr = nullptr;
    int *iptr = nullptr;

    if (ptr != nullptr) return 1;
    if (iptr != nullptr) return 2;

    if (ptr != iptr) {
        // both are null, they should be equal
        return 3;
    }

    return 0;
}
