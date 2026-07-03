int main(void) {
    unsigned int x = 0x5481f04fU;
    int result = 9;

    if (0xca404523830ae734LL > x) {
        result = 8;
    }

    return result == 8 ? 42 : result;
}
