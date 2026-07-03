typedef unsigned char uint8_t;

int main(void) {
    uint8_t x = 137;
    unsigned long y = 0x8939f5fa3187110eUL;

    if (!((x ^ y) > 0x63700168U)) return 1;
    if ((x & y) != 8UL) return 2;
    if (!((x | y) > 0x63700168U)) return 3;
    return 42;
}
