static int g = 0x12345678;

int main(void) {
    int *p = &g;
    (*p) &= 0xff;
    if (g != 0x78) {
        return 1;
    }
    return 42;
}
