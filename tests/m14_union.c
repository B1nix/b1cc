/* tests/m14_union.c — Test union type support (basic parse/compile test) */
union U {
    int i;
    char c;
};

int main(void) {
    union U u;
    u.i = 65;  /* 0x41 = 'A' */
    /* On little-endian, c should be 65 */
    if (u.c != 65) return 1;
    return 0;
}
