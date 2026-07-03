typedef unsigned char uint8_t;

int main(void) {
    uint8_t values[2];
    uint8_t *p;
    values[0] = 0xA4;
    values[1] = 0xA4;
    p = &values[1];
    ++values[1];
    *p = values[0];
    if (values[0] < 100) {
        return 1;
    }
    if (*p != 0xA4) {
        return 2;
    }
    return 42;
}
