typedef signed char int8_t;
typedef short int16_t;

int main(void) {
    int16_t s = 0xDE98;
    int8_t c = 0xA9;
    if (s >= 0) {
        return 1;
    }
    if (s != -8552) {
        return 2;
    }
    if (c >= 0) {
        return 3;
    }
    if (c != -87) {
        return 4;
    }
    s = 0x7fff;
    s = s + 1;
    if (s != -32768) {
        return 5;
    }
    return 42;
}
