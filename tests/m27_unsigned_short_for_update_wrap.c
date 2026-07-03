typedef unsigned short uint16_t;

int main(void) {
    uint16_t x;
    int count = 0;

    for (x = (uint16_t)-3; x >= 2; ++x) {
        count = count + 1;
        if (count > 10) return 1;
    }

    if (count != 3) return 2;
    return 42;
}
