typedef unsigned long uint64_t;

int main(void) {
    uint64_t a = 0x7cd95da2282d564cUL;
    uint64_t b = 18446744073709551615UL;
    short x = a % b;

    return x == 22092 ? 42 : 1;
}
