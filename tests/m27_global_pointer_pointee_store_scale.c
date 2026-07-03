static signed char g8 = 0x31;
static signed char *gp = &g8;

int main(void) {
    (*gp) ^= 0xB8;
    if (g8 != (signed char)0x89) {
        return 1;
    }
    return 42;
}
