typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

int main(void) {
    uint32_t x = 0x91E53BDBU;
    uint32_t *p = &x;
    uint8_t limit = 254U;
    if ((++(*p)) < limit) return 1;
    return 42;
}
