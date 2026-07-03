typedef unsigned char uint8_t;
typedef int int32_t;

uint8_t ret_u8(void) {
    int32_t x = -1;
    return x;
}

uint8_t shl_u8(uint8_t x, int s) {
    return x << s;
}

int main(void) {
    if (ret_u8() != 255) {
        return 1;
    }
    if (shl_u8(ret_u8(), 6) != 192) {
        return 2;
    }
    return 42;
}
