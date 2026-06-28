/* tests/m14_cast_trunc.c — Test integer cast truncation */
int main(void) {
    int x = 300;
    char c = (char)x;    /* 300 & 0xFF = 44 */
    if (c != 44) return 1;

    int y = -1;
    char d = (char)y;    /* -1 as char = -1 (0xFF sign-extended) */
    if (d != -1) return 2;

    int z = 65536;
    short s = (short)z;  /* 65536 & 0xFFFF = 0 */
    if (s != 0) return 3;

    return 0;
}
