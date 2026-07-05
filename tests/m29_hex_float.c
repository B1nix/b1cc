/* tests/m29_hex_float.c — M29 hexadecimal floating constants test. */

int main(void) {
    double a = 0x1.5p3;     /* (1 + 5/16) * 8 = 10.5 */
    double b = 0x1p4;       /* 1 * 16 = 16.0 */
    double c = 0x.8p0;      /* 8/16 * 1 = 0.5 */
    double d = 0x1.5p-1;    /* 1.3125 * 0.5 = 0.65625 */

    if ((int)a != 10) return 1;
    if ((int)(a * 2.0) != 21) return 2;
    if ((int)b != 16) return 3;
    if (c != 0.5) return 4;
    if (d != 0.65625) return 5;

    return 0;
}
