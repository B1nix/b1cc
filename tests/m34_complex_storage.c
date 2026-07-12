/* M34: double _Complex is a 16-byte storage type (two IEEE-754 doubles laid out
 * back-to-back). b1cc supports it as storage; complex arithmetic operators are
 * intentionally not implemented. */
double _Complex z;
int main(void) {
    if (sizeof(z) != 16) return 1;
    unsigned long *p = (unsigned long *)&z;
    p[0] = 0x4008000000000000UL;   /* 3.0 */
    p[1] = 0x4010000000000000UL;   /* 4.0 */
    if (p[0] != 0x4008000000000000UL) return 2;
    if (p[1] != 0x4010000000000000UL) return 3;
    return 42;
}
